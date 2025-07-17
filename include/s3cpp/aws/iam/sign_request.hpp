#pragma once

#include "s3cpp/aws/scope.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::iam {

[[nodiscard]] std::vector<uint8_t> get_signing_key(std::string_view secret_access_key, const Scope &scope);

[[nodiscard]] std::string sign_request(std::string_view access_key, std::string_view secret_access_key,
                                       std::string_view canonical_request, std::string_view signed_headers,
                                       std::string_view timestamp, const Scope &scope);

} // namespace s3cpp::aws::iam

//
#include "s3cpp/internal/macro-end.hpp"
