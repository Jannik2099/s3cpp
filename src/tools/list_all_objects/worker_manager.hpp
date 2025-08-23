#pragma once

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/meta.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace s3cpp::tools::list_all_objects {

class WorkerManager {
private:
    s3cpp::aws::s3::Client client;
    std::string bucket;
    std::shared_ptr<Metrics> metrics;
    std::shared_ptr<boost::asio::posix::stream_descriptor> output_file_stream;
    boost::asio::thread_pool &pool;
    WorkerScalingConfig config;
    ListObjectsApiVersion api_version;

    // Shared worker state
    std::shared_ptr<PrefixQueue> shared_prefix_queue = std::make_shared<PrefixQueue>();
    std::shared_ptr<std::mutex> shared_queue_mutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::atomic<std::size_t>> shared_workers_running_op =
        std::make_shared<std::atomic<std::size_t>>(0);

    std::atomic<std::size_t> target_workers{1};
    std::atomic<std::size_t> spawned_workers{0};
    std::chrono::steady_clock::time_point last_scaling_decision;

    [[nodiscard]] std::size_t calculate_desired_workers(double current_ops_per_second) const;
    [[nodiscard]] bool should_scale(std::chrono::steady_clock::time_point now) const;

public:
    WorkerManager(s3cpp::aws::s3::Client client, std::string bucket, std::shared_ptr<Metrics> metrics,
                  boost::asio::posix::stream_descriptor output_file_stream, boost::asio::thread_pool &pool,
                  WorkerScalingConfig config, ListObjectsApiVersion api_version);

    void adjust_workers(double current_ops_per_second);
    [[nodiscard]] std::size_t get_target_workers() const { return target_workers.load(); }
    [[nodiscard]] std::size_t get_spawned_workers() const { return spawned_workers.load(); }

    void ensure_workers_spawned();

private:
    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> spawn_worker();
};

} // namespace s3cpp::tools::list_all_objects
