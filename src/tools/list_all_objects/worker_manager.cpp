#include "worker_manager.hpp"

#include "misc.hpp"
#include "s3cpp/meta.hpp"
#include "worker.hpp"

#include <algorithm>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <print>
#include <s3cpp/aws/s3/client.hpp>
#include <string>
#include <utility>

namespace s3cpp::tools::list_all_objects {

WorkerManager::WorkerManager(s3cpp::aws::s3::Client client, std::string bucket,
                             std::shared_ptr<Metrics> metrics,
                             boost::asio::posix::stream_descriptor output_file_stream,
                             boost::asio::thread_pool &pool, WorkerScalingConfig config,
                             ListObjectsApiVersion api_version, OutputFormat output_format)
    : client{std::move(client)}, bucket{std::move(bucket)}, metrics{std::move(metrics)},
      output_file_stream{
          std::make_shared<boost::asio::posix::stream_descriptor>(std::move(output_file_stream))},
      pool{pool}, config{config}, api_version{api_version}, output_format{output_format}, target_workers{10},
      last_scaling_decision{std::chrono::steady_clock::now()} {

    // Initialize the shared queue with the root prefix (empty prefix)
    shared_prefix_queue->emplace(PrefixQueueEntry{.depth = 0, .paths = {{}}});
    this->metrics->total_queue_length++;
}

std::size_t WorkerManager::calculate_desired_workers(double current_ops_per_second) const {
    const std::size_t current_workers = metrics->active_workers.load();

    // If we have very few operations, don't scale down too aggressively
    if (current_ops_per_second < 1.0) {
        return std::max(current_workers, 10UL);
    }

    std::size_t desired_workers = current_workers;

    // Scale up if we have fewer workers than ops/second (workers are under-utilized)
    if (static_cast<double>(current_workers) < current_ops_per_second) {
        desired_workers = std::ceil(
            std::max(static_cast<double>(current_workers) * config.scale_up_factor, current_ops_per_second));
        std::println("Scaling up: {} workers < {:.2f} ops/s, {} -> {} workers", current_workers,
                     current_ops_per_second, current_workers, desired_workers);
    }
    // Scale down if we have significantly more workers than ops/second (workers are over-provisioned)
    else if (static_cast<double>(current_workers) > current_ops_per_second * 1.5) {
        desired_workers = std::ceil(std::max(static_cast<double>(current_workers) * config.scale_down_factor,
                                             current_ops_per_second));
        std::println("Scaling down: {} workers > {:.2f} ops/s * 1.5, {} -> {} workers", current_workers,
                     current_ops_per_second, current_workers, desired_workers);
    }

    return desired_workers;
}

bool WorkerManager::should_scale(std::chrono::steady_clock::time_point now) const {
    return now - last_scaling_decision >= config.scaling_interval;
}

void WorkerManager::adjust_workers(double current_ops_per_second) {
    const auto now = std::chrono::steady_clock::now();

    if (!should_scale(now)) {
        return;
    }

    const std::size_t desired_workers = calculate_desired_workers(current_ops_per_second);
    const std::size_t current_target = target_workers;

    if (desired_workers != current_target) {
        target_workers = desired_workers;
        last_scaling_decision = now;

        std::println("Worker scaling decision: {} -> {} workers (ops/s: {:.2f})", current_target,
                     desired_workers, current_ops_per_second);

        // Spawn additional workers if needed
        ensure_workers_spawned();
    }
}

void WorkerManager::ensure_workers_spawned() {
    const std::size_t target = target_workers;
    const std::size_t current_spawned = spawned_workers;

    // Spawn additional workers up to the target
    if (current_spawned < target) {
        const std::size_t workers_to_spawn = target - current_spawned;
        for (std::size_t i = 0; i < workers_to_spawn; i++) {
            spawned_workers++;
            boost::asio::co_spawn(pool, spawn_worker(), boost::asio::detached);
        }
    }
}

meta::crt<boost::asio::awaitable<void>> WorkerManager::spawn_worker() {
    // Create scaling refs for the worker to use
    const WorkerScalingRefs scaling_refs{.target_workers = &target_workers,
                                         .active_workers = &(metrics->active_workers)};

    // Create worker with shared queue and synchronization state
    Worker worker{client,
                  bucket,
                  metrics,
                  output_file_stream,
                  shared_prefix_queue,
                  shared_queue_mutex,
                  shared_workers_running_op,
                  api_version,
                  scaling_refs,
                  output_format};

    // Run the worker - this will process work and handle its own termination
    co_await worker.work();

    // Worker has finished, decrement spawned count
    spawned_workers--;
}

} // namespace s3cpp::tools::list_all_objects
