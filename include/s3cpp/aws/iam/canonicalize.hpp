#pragma once

#include "s3cpp/meta.hpp"

#include <boost/beast/http/fields.hpp> // IWYU pragma: keep
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::iam {

namespace _internal {

[[nodiscard]] std::tuple<std::string, std::string> canonicalize_request(std::string_view method_string,
                                                                        std::string_view encoded_target_str,
                                                                        std::span<const std::byte> body,
                                                                        boost::beast::http::fields &headers);

} // namespace _internal

// returns [CanonicalRequest, SignedHeaders]
template <typename Request>
[[nodiscard]] std::tuple<std::string, std::string> canonicalize_request(Request &request) {
    return _internal::canonicalize_request(
        request.method_string(), request.target(),
        std::span<const std::byte>{meta::safe_reinterpret_cast<const std::byte *>(request.body().data()),
                                   request.body().size()},
        request.base());
}

} // namespace s3cpp::aws::iam

//
#include "s3cpp/internal/macro-end.hpp"
