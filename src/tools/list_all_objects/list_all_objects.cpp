#include "misc.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/session.hpp"
#include "worker_manager.hpp"

#include <atomic>
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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
    ListObjectsApiVersion api_version{};

    double scale_up_factor{};
    double scale_down_factor{};
    std::size_t scaling_interval_seconds{};
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
        ("api-version", boost::program_options::value<std::string>()->default_value("v2"), "ListObjects API version to use (v1 or v2)")
        ("scale-up-factor", boost::program_options::value<double>(&ret.scale_up_factor)->default_value(1.2), "multiply workers by this factor when scaling up")
        ("scale-down-factor", boost::program_options::value<double>(&ret.scale_down_factor)->default_value(0.8), "multiply workers by this factor when scaling down")
        ("scaling-interval", boost::program_options::value<std::size_t>(&ret.scaling_interval_seconds)->default_value(1), "scaling check interval in seconds")
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

    const std::string api_version_str = varmap["api-version"].as<std::string>();
    if (api_version_str == "v1") {
        ret.api_version = s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1;
    } else if (api_version_str == "v2") {
        ret.api_version = s3cpp::tools::list_all_objects::ListObjectsApiVersion::V2;
    } else {
        std::println(std::cerr, "Invalid API version '{}'. Must be 'v1' or 'v2'.", api_version_str);
        exit(1);
    }

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

    // Create worker scaling configuration from command line options
    const WorkerScalingConfig scaling_config{.scale_up_factor = options.scale_up_factor,
                                             .scale_down_factor = options.scale_down_factor,
                                             .scaling_interval =
                                                 std::chrono::seconds{options.scaling_interval_seconds}};

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

            std::println("{} active workers, {} ops in flight, {} queued ops, {} total ops, {} total "
                         "objects, {:.2f} objects/s, "
                         "{:.2f} ops/s",
                         metrics->active_workers.load(), metrics->ops_in_flight.load(),
                         metrics->total_queue_length.load(), metrics->total_ops.load(),
                         metrics->total_objects_found.load(), rolling_mean(objects_accumulator),
                         rolling_mean(ops_accumulator));
            std::flush(std::cout);
        }
    }};

    const auto file_strand = boost::asio::make_strand(pool);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto output_file_fd = open(options.output_file.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
                                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (output_file_fd == -1) {
        std::println(std::cerr, "failed to open output file {}: {}", options.output_file, strerror(errno));
        return 1;
    }
    boost::asio::posix::stream_descriptor output_file_stream{file_strand, output_file_fd};

    // Create worker manager for dynamic scaling
    WorkerManager worker_manager{
        client,         options.bucket,     metrics, std::move(output_file_stream), pool,
        scaling_config, options.api_version};

    // Start initial workers
    worker_manager.ensure_workers_spawned();

    // Start scaling management thread
    const std::jthread scaling_thread{[&worker_manager, metrics](const std::stop_token &token) {
        using namespace boost::accumulators;
        accumulator_set<std::size_t, stats<tag::rolling_mean>> ops_accumulator{
            tag::rolling_window::window_size = 60};

        std::size_t previous_ops = 0;

        while (!token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds{1});

            const std::size_t current_total_ops = metrics->total_ops.load();
            const std::size_t new_ops = current_total_ops - previous_ops;
            previous_ops = current_total_ops;

            ops_accumulator(new_ops);
            const double current_ops_per_second = rolling_mean(ops_accumulator);

            // Adjust workers based on current performance
            worker_manager.adjust_workers(current_ops_per_second);
        }
    }};

    pool.join();
}
