#pragma once

#include "s3cpp/aws/s3/types.hpp"

#include <atomic>
#include <compare>
#include <cstddef>
#include <functional>
#include <queue>
#include <vector>

namespace s3cpp::tools::list_all_objects {

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
};

} // namespace s3cpp::tools::list_all_objects
