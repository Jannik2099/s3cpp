#pragma once

#include "s3cpp/aws/scope.hpp"
#include "s3cpp/meta.hpp"

#include <chrono>
#include <format>
#include <string>
#include <string_view>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::iam {

template <typename T>
    requires meta::is_specialization_v<T, std::chrono::time_point>
[[nodiscard]] std::string format_timestamp(const T &time) {
    // TODO: convert non-utc
    // some endpoints won't work with the +offset format
    return std::format("{0:%Y%m%d}T{0:%H%M}{0:%OS}Z", time);
}

[[nodiscard]] std::string string_to_sign(std::string_view canonical_request, const Scope &scope,
                                         std::string_view timestamp);

} // namespace s3cpp::aws::iam

//
#include "s3cpp/internal/macro-end.hpp"
