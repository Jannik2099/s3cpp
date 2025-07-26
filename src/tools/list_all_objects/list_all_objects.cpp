#include "misc.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/session.hpp"
#include "worker.hpp"

#include <atomic>
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <print>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

using namespace s3cpp::tools::list_all_objects;

namespace {

[[nodiscard]] std::string file_to_string(const std::filesystem::path &path) {
    const std::ifstream stream{path};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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
    const Options options = parse_opts(argc, argv);

    boost::asio::thread_pool pool{std::thread::hardware_concurrency()};

    const auto session = std::make_shared<s3cpp::aws::s3::Session>(
        s3cpp::aws::iam::Session{.access_key = options.access_key,
                                 .secret_access_key = options.secret_key,
                                 .region = "default",
                                 .endpoint = boost::urls::url{options.endpoint}},
        pool.get_executor());
    const s3cpp::aws::s3::Client client{session};
    const auto metrics = std::make_shared<Metrics>();

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

    const auto file_strand = boost::asio::make_strand(pool);
    boost::asio::stream_file output_file_stream{file_strand, options.output_file,
                                                boost::asio::stream_file::write_only |
                                                    boost::asio::stream_file::create |
                                                    boost::asio::stream_file::truncate};

    Worker worker{client, options.bucket, metrics, std::move(output_file_stream)};

    for (std::size_t i = 0; i < options.max_ops_in_flight; i++) {
        boost::asio::co_spawn(pool, worker.work(), boost::asio::detached);
    }

    pool.join();
}
