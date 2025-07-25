#include "worker.hpp"

#include "misc.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <algorithm>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/error.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr auto token = boost::asio::as_tuple(boost::asio::use_awaitable);

} // namespace

namespace s3cpp::tools::list_all_objects {

meta::crt<boost::asio::awaitable<bool>> Worker::is_done() {
    while (true) {
        bool queue_empty{};
        {
            const std::scoped_lock lock{queue_mutex};
            queue_empty = prefix_queue.empty();
            if (queue_empty && stats->ops_in_flight == 0 && workers_running_op == 0) {
                // nothing in the queue and no work in flight
                // we are done
                co_return true;
            }
        }
        if (queue_empty) {
            // queue was empty but there is still work in flight
            // suspend and resume the loop, as the outstanding work will generate new queue entries
            // std::println("no work, suspending. {} ops in flight", ops_in_flight->load());
            boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor,
                                            std::chrono::steady_clock::now() +
                                                std::chrono::milliseconds{100}};
            co_await timer.async_wait();
            continue;
        }
        co_return false;
    }
}

meta::crt<boost::asio::awaitable<std::optional<aws::s3::CommonPrefix>>> Worker::get_next_prefix() {
    const std::scoped_lock lock{queue_mutex};
    const PrefixQueueEntry *top{};
    while (true) {
        // someone took the last work element from the queue while we were waiting on the lock
        if (prefix_queue.empty()) {
            co_return std::nullopt;
        }
        const PrefixQueueEntry &maybe_top = prefix_queue.top();
        if (maybe_top.paths.empty()) {
            prefix_queue.pop();
        } else {
            top = &maybe_top;
            break;
        }
    }
    const aws::s3::CommonPrefix ret = std::move(top->paths.back());
    top->paths.pop_back();
    stats->total_queue_length--;
    workers_running_op++;
    co_return ret;
}

meta::crt<boost::asio::awaitable<aws::s3::ListBucketResult>>
Worker::list_one(std::optional<std::string> prefix, std::optional<std::string> continuation_token) {
    const std::string my_prefix = prefix.value_or("<no prefix>");
    for (int retries = 0; retries < 5; retries++) {
        auto res = co_await client.list_objects_v2(
            {.Bucket = bucket, .ContinuationToken = continuation_token, .Delimiter = "/", .Prefix = prefix});
        if (!res) {
            struct Visitor {
                static std::string operator()(const boost::beast::error_code &error) { return error.what(); }
                static std::string operator()(const pugi::xml_parse_status &error) {
                    return std::format("pugixml error {}", std::to_underlying(error));
                }
            };
            const auto errstr = std::visit(Visitor{}, res.error());
            std::println(std::cerr, "ERROR in prefix {} {} - retrying", my_prefix, errstr);
            if (std::holds_alternative<boost::beast::error_code>(res.error())) {
                continue;
            }
        }

        co_return res.value();
    }
    std::println("no success after 5 retries on prefix {}", my_prefix);
    co_return aws::s3::ListBucketResult{};
}

meta::crt<boost::asio::awaitable<void>> Worker::process_prefix(aws::s3::CommonPrefix prefix) {
    std::optional<std::string> continuation_token;
    while (true) {
        const std::size_t depth = std::ranges::count(prefix.Prefix.value_or(""), '/');
        stats->ops_in_flight++;
        aws::s3::ListBucketResult res = co_await list_one(prefix.Prefix, std::move(continuation_token));
        continuation_token = std::move(res.NextContinuationToken);

        co_await write_objects(res.Contents.value_or(std::vector<aws::s3::Object>{}));

        {
            const std::scoped_lock lock{queue_mutex};
            stats->total_objects_found += res.Contents.value_or(std::vector<aws::s3::Object>{}).size();
            stats->total_queue_length +=
                res.CommonPrefixes.value_or(std::vector<aws::s3::CommonPrefix>{}).size();
            prefix_queue.emplace(
                depth, std::move(res.CommonPrefixes).value_or(std::vector<aws::s3::CommonPrefix>{}));
            stats->ops_in_flight--;
            stats->total_ops++;
        }
        if (!continuation_token.has_value()) {
            break;
        }
    }
    workers_running_op--;
}

meta::crt<boost::asio::awaitable<void>> Worker::write_objects(std::span<const aws::s3::Object> objects) {
    std::size_t required_size{};
    for (const auto &object : objects) {
        if (!object.Key.has_value()) {
            std::println(std::cerr, "ERROR received object without key, ETag {}",
                         object.ETag.value_or("<no ETag>"));
            continue;
        }
        required_size += object.Key->size() + 1;
    }

    std::string string_buf;
    string_buf.reserve(required_size);

    for (const auto &object : objects) {
        if (object.Key.has_value()) {
            string_buf += *object.Key;
            string_buf += '\n';
        }
    }

    const boost::asio::const_buffer buf{string_buf.data(), string_buf.size()};
    // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
    const auto [error, bytes] = co_await boost::asio::async_write(output_file_stream, buf, token);
    if (error) {
        std::println(std::cerr, "ERROR writing to output file: {}", error.what());
    }
}

meta::crt<boost::asio::awaitable<void>> Worker::work() {
    while (true) {
        if (co_await is_done()) {
            co_return;
        }
        const std::optional<aws::s3::CommonPrefix> next_prefix = co_await get_next_prefix();
        if (!next_prefix.has_value()) {
            continue;
        }
        co_await process_prefix(*next_prefix);
    }
}

Worker::Worker(aws::s3::Client client, std::string bucket, std::shared_ptr<Metrics> stats,
               boost::asio::posix::stream_descriptor output_file_stream)
    : client{std::move(client)}, bucket{std::move(bucket)}, stats{std::move(stats)},
      output_file_stream{std::move(output_file_stream)} {
    prefix_queue.emplace(PrefixQueueEntry{.depth = 0, .paths = {{}}});
    this->stats->total_queue_length++;
}

} // namespace s3cpp::tools::list_all_objects
