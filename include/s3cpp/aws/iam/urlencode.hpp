#pragma once

#include <string>
#include <string_view>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::iam {

[[nodiscard]] std::string urlencode(std::string_view input);
[[nodiscard]] bool urlencode_required(std::string_view input);
[[nodiscard]] std::string urlencode_path(std::string_view input);
[[nodiscard]] bool urlencode_path_required(std::string_view input);
[[nodiscard]] std::string urlencode_query(std::string_view input);
[[nodiscard]] bool urlencode_query_required(std::string_view input);

} // namespace s3cpp::aws::iam

//
#include "s3cpp/internal/macro-end.hpp"
