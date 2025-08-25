#pragma once

#include "s3cpp/aws/s3/types.hpp"

#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace s3cpp::tools::list_all_objects {

enum class OutputFormat : std::uint8_t { PLAIN, JSON };

enum class ListObjectsApiVersion : std::uint8_t { V1, V2 };

struct PrefixQueueEntry {
    std::size_t depth = 0;
    mutable std::vector<aws::s3::CommonPrefix> paths; // pq.top() returns a const ref

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
    std::atomic<std::size_t> active_workers;
    std::atomic<std::size_t> target_workers;
};

struct WorkerScalingConfig {
    double scale_up_factor{};
    double scale_down_factor{};
    std::chrono::seconds scaling_interval{};
};

} // namespace s3cpp::tools::list_all_objects
