#pragma once

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/lockfree/stack.hpp>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>

namespace s3cpp::tools::list_all_objects {

class Worker {
private:
    aws::s3::Client client;
    std::string bucket;
    std::shared_ptr<Metrics> stats;
    std::shared_ptr<boost::lockfree::stack<std::string>> write_stack;
    ListObjectsApiVersion api_version;

    // Shared queue and synchronization - references to WorkerManager's state
    std::shared_ptr<PrefixQueue> prefix_queue;
    std::shared_ptr<std::mutex> queue_mutex;
    std::shared_ptr<std::atomic<std::size_t>> workers_running_op;

    OutputFormat output_format;

    [[nodiscard]] meta::crt<boost::asio::awaitable<bool>> is_done();
    [[nodiscard]] meta::crt<
        boost::asio::awaitable<std::optional<std::tuple<aws::s3::CommonPrefix, std::size_t>>>>
    get_next_prefix();
    [[nodiscard]] meta::crt<
        boost::asio::awaitable<std::variant<aws::s3::ListObjectsResult, aws::s3::ListObjectsV2Result>>>
    list_one(std::optional<std::string> prefix, std::optional<std::string> continuation_token);
    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> process_prefix(aws::s3::CommonPrefix prefix,
                                                                         std::size_t depth);
    void write_objects(std::span<const aws::s3::Object> objects);

    [[nodiscard]] bool should_terminate_early() const;

public:
    [[nodiscard]] Worker(aws::s3::Client client, std::string bucket, std::shared_ptr<Metrics> stats,
                         std::shared_ptr<boost::lockfree::stack<std::string>> write_stack,
                         std::shared_ptr<PrefixQueue> prefix_queue, std::shared_ptr<std::mutex> queue_mutex,
                         std::shared_ptr<std::atomic<std::size_t>> workers_running_op,
                         ListObjectsApiVersion api_version, OutputFormat output_format);

    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> work();
};

} // namespace s3cpp::tools::list_all_objects
