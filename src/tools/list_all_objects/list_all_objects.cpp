#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/session.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <algorithm>
#include <atomic>
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <print>
#include <queue>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct PrefixQueueEntry {
    std::size_t depth = 0;
    mutable std::vector<s3cpp::aws::s3::CommonPrefix> paths; // pq.top() returns a const ref

    [[nodiscard]] std::weak_ordering operator<=>(const PrefixQueueEntry &rhs) const noexcept {
        return depth <=> rhs.depth;
    }
};
using PrefixQueue = std::priority_queue<PrefixQueueEntry, std::vector<PrefixQueueEntry>, std::greater<>>;

struct Metrics {
    std::atomic<std::size_t> ops_in_flight;
    std::atomic<std::size_t> total_ops;
    std::atomic<std::size_t> total_queue_length;
    std::atomic<std::size_t> total_objects_found;
};

[[nodiscard]] std::string file_to_string(const std::filesystem::path &path) {
    const std::ifstream stream{path};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] s3cpp::meta::crt<boost::asio::awaitable<s3cpp::aws::s3::ListBucketResult>>
list_prefix(std::shared_ptr<const s3cpp::aws::s3::Client> client, std::string bucket,
            std::optional<std::string> prefix, std::optional<std::string> continuation_token) {
    const auto my_prefix = prefix.value_or("");
    for (int retries = 0; retries < 5; retries++) {
        auto res = co_await client->list_objects_v2(
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
            if (std::holds_alternative<boost::system::error_code>(res.error())) {
                continue;
            }
        }

        co_return res.value();
    }
    std::println("no success after 5 retries on prefix {}", my_prefix);
    co_return s3cpp::aws::s3::ListBucketResult{};
}

[[nodiscard]] s3cpp::meta::crt<boost::asio::awaitable<void>>
worker(std::shared_ptr<const s3cpp::aws::s3::Client> client, std::string bucket,
       std::shared_ptr<PrefixQueue> prefix_queue, std::shared_ptr<std::mutex> queue_mutex,
       std::shared_ptr<Metrics> stats, std::shared_ptr<boost::asio::stream_file> output_file_stream) {
    static std::atomic<std::size_t> workers_running_op;

    while (true) {
        {
            bool queue_empty{};
            {
                const std::scoped_lock lock{*queue_mutex};
                queue_empty = prefix_queue->empty();
                if (queue_empty && stats->ops_in_flight == 0 && workers_running_op == 0) {
                    // nothing in the queue and no work in flight
                    // we are done
                    co_return;
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
        }
        {
            s3cpp::aws::s3::CommonPrefix new_prefix;
            {
                const std::scoped_lock lock{*queue_mutex};
                if (prefix_queue->empty()) {
                    // someone took the last work element from the queue while we were waiting on the lock
                    // return to top to suspend again or exit
                    continue;
                }
                const PrefixQueueEntry &top = prefix_queue->top();
                if (top.paths.empty()) {
                    prefix_queue->pop();
                    continue;
                }
                new_prefix = std::move(top.paths.back());
                top.paths.pop_back();
                stats->total_queue_length--;
                workers_running_op++;
            }

            std::optional<std::string> continuation_token;
            while (true) {

                const std::size_t depth = std::ranges::count(new_prefix.Prefix.value_or(""), '/');
                stats->ops_in_flight++;
                s3cpp::aws::s3::ListBucketResult res = co_await list_prefix(
                    client, bucket, std::move(new_prefix.Prefix), std::move(continuation_token));
                continuation_token = std::move(res.NextContinuationToken);

                for (const auto &object : res.Contents.value_or(std::vector<s3cpp::aws::s3::Object>{})) {
                    if (!object.Key.has_value()) {
                        std::println(std::cerr, "ERROR received object without key, ETag {}",
                                     object.ETag.value_or(""));
                        continue;
                    }
                    std::string key = object.Key.value();
                    key.push_back('\n');
                    const boost::asio::const_buffer buf{key.data(), key.size()};
                    co_await output_file_stream->async_write_some(buf);
                }
                {
                    const std::scoped_lock lock{*queue_mutex};
                    stats->total_objects_found +=
                        res.Contents.value_or(std::vector<s3cpp::aws::s3::Object>{}).size();
                    stats->total_queue_length +=
                        res.CommonPrefixes.value_or(std::vector<s3cpp::aws::s3::CommonPrefix>{}).size();
                    prefix_queue->emplace(
                        depth,
                        std::move(res.CommonPrefixes).value_or(std::vector<s3cpp::aws::s3::CommonPrefix>{}));
                    stats->ops_in_flight--;
                    stats->total_ops++;
                    workers_running_op--;
                }
                if (!continuation_token.has_value()) {
                    break;
                }
            }
        }
    }
}

struct Options {
    std::string bucket;
    std::string endpoint;
    std::string access_key;
    std::string secret_key;
    std::string output_file;
    std::size_t max_ops_in_flight{};
};

[[nodiscard]] Options parse_opts(int argc, char **argv) {

    Options ret;

    boost::program_options::options_description descr{"Options"};
    // clang-format off
    descr.add_options()
        ("help,h", "print this help")
        ("bucket,b",  boost::program_options::value<std::string>(&ret.bucket)->required(), "S3 bucket name")
        ("endpoint,e",  boost::program_options::value<std::string>(&ret.endpoint)->required(), "endpoint URL, including protocol and (if required) port")
        ("access-key-file", boost::program_options::value<std::string>(&ret.access_key)->required(), "path to access key file")
        ("secret-access-key-file", boost::program_options::value<std::string>(&ret.secret_key)->required(), "path to secret key file")
        ("output-file,o", boost::program_options::value<std::string>(&ret.output_file)->required(), "path to output file")
        ("max-ops-in-flight", boost::program_options::value<std::size_t>(&ret.max_ops_in_flight)->default_value(100), "maximum concurrent amount of requests")
    ;
    // clang-format on

    boost::program_options::variables_map varmap;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, descr), varmap);
    if (varmap.contains("help")) {
        std::println("list objects in a S3 bucket\n"
                     "output order is not stable\n");
        std::cout << descr << '\n';
        exit(0);
    }
    boost::program_options::notify(varmap);

    ret.access_key = file_to_string(ret.access_key);
    ret.secret_key = file_to_string(ret.secret_key);
    boost::algorithm::trim(ret.access_key);
    boost::algorithm::trim(ret.secret_key);

    return ret;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {

    boost::asio::thread_pool pool{std::thread::hardware_concurrency()};

    const Options options = parse_opts(argc, argv);
    const auto prefix_queue = std::make_shared<PrefixQueue>();
    PrefixQueueEntry first_elem{.depth = 0, .paths = {{}}};
    prefix_queue->emplace(std::move(first_elem));
    const auto queue_mutex = std::make_shared<std::mutex>();

    const auto session = std::make_shared<s3cpp::aws::s3::Session>(
        s3cpp::aws::iam::Session{.access_key = options.access_key,
                                 .secret_access_key = options.secret_key,
                                 .region = "default",
                                 .endpoint = boost::urls::url{options.endpoint}},
        pool.get_executor());
    const auto client = std::make_shared<const s3cpp::aws::s3::Client>(session);
    const auto metrics = std::make_shared<Metrics>();
    metrics->total_queue_length++;

    const std::jthread stats_thread{[metrics](const std::stop_token &token) {
        using namespace boost::accumulators;
        accumulator_set<std::size_t, stats<tag::rolling_mean>> objects_accumulator{
            tag::rolling_window::window_size = 300};
        accumulator_set<std::size_t, stats<tag::rolling_mean>> ops_accumulator{
            tag::rolling_window::window_size = 300};

        while (!token.stop_requested()) {
            const std::size_t previous_objects = metrics->total_objects_found;
            const std::size_t previous_ops = metrics->total_ops;
            std::this_thread::sleep_for(std::chrono::seconds{1});
            const std::size_t new_objects = metrics->total_objects_found - previous_objects;
            const std::size_t new_ops = metrics->total_ops - previous_ops;
            objects_accumulator(new_objects);
            ops_accumulator(new_ops);

            std::println("{} ops in flight, {} queued ops, {} total ops, {} total objects, {:.2f} objects/s, "
                         "{:.2f} ops/s",
                         metrics->ops_in_flight.load(), metrics->total_queue_length.load(),
                         metrics->total_ops.load(), metrics->total_objects_found.load(),
                         rolling_mean(objects_accumulator), rolling_mean(ops_accumulator));
            std::flush(std::cout);
        }
    }};

    const auto output_file_stream = std::make_shared<boost::asio::stream_file>(
        pool, options.output_file,
        boost::asio::stream_file::write_only | boost::asio::stream_file::create |
            boost::asio::stream_file::truncate);
    for (std::size_t i = 0; i < options.max_ops_in_flight; i++) {
        boost::asio::co_spawn(
            pool, worker(client, options.bucket, prefix_queue, queue_mutex, metrics, output_file_stream),
            boost::asio::detached);
    }

    pool.join();
}
