#pragma once

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/meta.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/lockfree/stack.hpp>
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
    std::shared_ptr<boost::lockfree::stack<std::string>> write_stack =
        std::make_shared<boost::lockfree::stack<std::string>>(1024);
    std::unique_ptr<boost::asio::thread_pool> pool;
    WorkerScalingConfig config;
    ListObjectsApiVersion api_version;
    OutputFormat output_format;

    // Shared worker state
    std::shared_ptr<PrefixQueue> shared_prefix_queue = std::make_shared<PrefixQueue>();
    std::shared_ptr<std::mutex> shared_queue_mutex = std::make_shared<std::mutex>();

    [[nodiscard]] std::size_t calculate_desired_workers(double current_ops_per_second) const;

    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> spawn_worker();

    // wrapped in unique_ptr so that the class stays moveable
    std::unique_ptr<boost::asio::cancellation_signal> scaling_cancellation_signal =
        std::make_unique<boost::asio::cancellation_signal>();
    [[nodiscard]] meta::crt<boost::asio::awaitable<void>>
    scaling_worker(boost::asio::cancellation_slot cancellation_slot);

public:
    [[nodiscard]] WorkerManager(s3cpp::aws::s3::Client client, std::string bucket,
                                std::shared_ptr<Metrics> metrics,
                                boost::asio::posix::stream_descriptor output_file_stream,
                                std::unique_ptr<boost::asio::thread_pool> pool, WorkerScalingConfig config,
                                ListObjectsApiVersion api_version, OutputFormat output_format);
    ~WorkerManager();

    [[nodiscard]] WorkerManager(const WorkerManager &) = delete;
    [[nodiscard]] WorkerManager &operator=(const WorkerManager &) = delete;
    [[nodiscard]] WorkerManager(WorkerManager &&) = default;
    [[nodiscard]] WorkerManager &operator=(WorkerManager &&) = default;
};

} // namespace s3cpp::tools::list_all_objects
