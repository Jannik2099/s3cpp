#pragma once

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/stream_file.hpp>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace s3cpp::tools::list_all_objects {

class Worker {
private:
    aws::s3::Client client;
    std::string bucket;
    std::shared_ptr<Metrics> stats;
    boost::asio::stream_file output_file_stream;

    PrefixQueue prefix_queue;
    std::mutex queue_mutex;
    std::atomic<std::size_t> workers_running_op;

    [[nodiscard]] meta::crt<boost::asio::awaitable<bool>> is_done();
    [[nodiscard]] meta::crt<boost::asio::awaitable<std::optional<aws::s3::CommonPrefix>>> get_next_prefix();
    [[nodiscard]] meta::crt<boost::asio::awaitable<aws::s3::ListBucketResult>>
    list_one(std::optional<std::string> prefix, std::optional<std::string> continuation_token);
    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> process_prefix(aws::s3::CommonPrefix prefix);
    [[nodiscard]] meta::crt<boost::asio::awaitable<void>>
    write_objects(std::span<const aws::s3::Object> objects);

public:
    [[nodiscard]] Worker(aws::s3::Client client, std::string bucket, std::shared_ptr<Metrics> stats,
                         boost::asio::stream_file output_file_stream);

    [[nodiscard]] meta::crt<boost::asio::awaitable<void>> work();
};

} // namespace s3cpp::tools::list_all_objects
