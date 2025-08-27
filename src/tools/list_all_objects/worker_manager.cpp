#include "worker_manager.hpp"

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/meta.hpp"
#include "worker.hpp"

#include <algorithm>
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/scope/scope_exit.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <thread>
#include <utility>

namespace {

s3cpp::meta::crt<boost::asio::awaitable<void>>
writer_coroutine(std::shared_ptr<s3cpp::tools::list_all_objects::Metrics> metrics,
                 boost::asio::posix::stream_descriptor output_file_stream,
                 std::shared_ptr<boost::lockfree::stack<std::string>> write_stack) {
    constexpr auto token = boost::asio::as_tuple(boost::asio::use_awaitable);

    // The evaluation order matters here.
    // Workers push to the stack before exiting.
    // But in the other direction, we might check for an empty stack, have a worker write to
    // it and exit, and then see that there's no active workers
    while (metrics->active_workers > 0 || !write_stack->empty()) {
        std::string str;

        if (write_stack->pop(str)) {
            const boost::asio::const_buffer buf{str.data(), str.size()};
            const auto [error, bytes_written] =
                co_await boost::asio::async_write(output_file_stream, buf, token);

            if (error) {
                std::println(std::cerr, "ERROR writing to output file: {}", error.what());
            }
        } else {
            // No data available, sleep briefly before checking again
            boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor,
                                            std::chrono::milliseconds{10}};
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }
}

} // namespace

namespace s3cpp::tools::list_all_objects {

WorkerManager::WorkerManager(s3cpp::aws::s3::Client client, std::string bucket,
                             std::shared_ptr<Metrics> metrics,
                             boost::asio::posix::stream_descriptor output_file_stream,
                             std::unique_ptr<boost::asio::thread_pool> pool, WorkerScalingConfig config,
                             ListObjectsApiVersion api_version, OutputFormat output_format)
    : client{std::move(client)}, bucket{std::move(bucket)}, metrics{std::move(metrics)},
      pool{std::move(pool)}, config{config}, api_version{api_version}, output_format{output_format} {
    // Initialize the shared queue with the root prefix (empty prefix)
    shared_prefix_queue->emplace(PrefixQueueEntry{.depth = 0, .paths = {{}}});
    this->metrics->total_queue_length++;
    this->metrics->target_workers = std::max(this->metrics->target_workers.load(), 1UL);

    boost::asio::co_spawn(*this->pool, scaling_worker(scaling_cancellation_signal->slot()),
                          boost::asio::detached);
    // we need to wait for a worker to start so that the writer doesn't immediately exit
    while (this->metrics->total_ops == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    boost::asio::co_spawn(*this->pool,
                          writer_coroutine(this->metrics, std::move(output_file_stream), write_stack),
                          boost::asio::detached);
}

WorkerManager::~WorkerManager() {
    while (true) {
        {
            const std::scoped_lock lock{*shared_queue_mutex};
            if (shared_prefix_queue->empty() && metrics->active_workers == 0) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1000});
    }
    scaling_cancellation_signal->emit(boost::asio::cancellation_type::total);
    pool->join();
}

meta::crt<boost::asio::awaitable<void>>
WorkerManager::scaling_worker(boost::asio::cancellation_slot cancellation_slot) {
    using namespace boost::accumulators;
    accumulator_set<std::size_t, stats<tag::rolling_mean>> ops_accumulator{tag::rolling_window::window_size =
                                                                               60};

    std::size_t previous_ops = 0;

    while (true) {
        boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, config.scaling_interval};
        const auto [wait_ec] = co_await timer.async_wait(boost::asio::bind_cancellation_slot(
            cancellation_slot, boost::asio::as_tuple(boost::asio::use_awaitable)));
        if (wait_ec.value() == boost::asio::error::operation_aborted) {
            co_return;
        } else if (wait_ec.failed()) {
            throw boost::system::system_error{wait_ec};
        }

        const std::size_t current_total_ops = metrics->total_ops.load();
        const std::size_t new_ops = current_total_ops - previous_ops;
        previous_ops = current_total_ops;

        ops_accumulator(new_ops);
        const double current_ops_per_second = rolling_mean(ops_accumulator);

        const std::size_t desired_workers = calculate_desired_workers(current_ops_per_second);
        const std::size_t current_target = metrics->target_workers;

        if (desired_workers != current_target) {
            metrics->target_workers = desired_workers;

            const std::size_t current_running = metrics->active_workers;

            // Spawn additional workers up to the target
            if (current_running < desired_workers) {
                const std::size_t workers_to_spawn = desired_workers - current_running;
                for (std::size_t i = 0; i < workers_to_spawn; i++) {
                    boost::asio::co_spawn(*pool, spawn_worker(), boost::asio::detached);
                }
            }
        }
    }
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

    // Always keep at least one worker. We have other logic that relies on active_workers > 0 to detect
    // whether the job is still running.
    // Otherwise, we could theoretically scale down to zero while there's still work in the queue.
    desired_workers = std::max(desired_workers, 1UL);

    return desired_workers;
}

s3cpp::meta::crt<boost::asio::awaitable<void>> WorkerManager::spawn_worker() {
    Worker worker{client,      bucket,       metrics, write_stack, shared_prefix_queue, shared_queue_mutex,
                  api_version, output_format};

    (metrics->active_workers)++;
    const boost::scope::scope_exit decrement_active_workers{[this]() { (metrics->active_workers)--; }};
    co_await worker.work();
}

} // namespace s3cpp::tools::list_all_objects
