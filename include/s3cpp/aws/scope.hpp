#pragma once

#include <format>
#include <string>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws {

struct Scope {
    std::string timestamp;
    std::string region;
    std::string service;
    constexpr static std::string footer = "aws4_request";
};

} // namespace s3cpp::aws

template <> struct std::formatter<s3cpp::aws::Scope> {
    [[nodiscard]] constexpr static auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

    static auto format(const s3cpp::aws::Scope &scope, std::format_context &ctx) {
        return std::format_to(ctx.out(), "{}/{}/{}/{}", scope.timestamp, scope.region, scope.service,
                              s3cpp::aws::Scope::footer);
    }
};

//
#include "s3cpp/internal/macro-end.hpp"
