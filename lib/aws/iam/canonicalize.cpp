#include "s3cpp/aws/iam/canonicalize.hpp"

#include "s3cpp/meta.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp> // IWYU pragma: keep
#include <boost/url/param.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>
#include <botan/hash.h>
#include <botan/hex.h>
#include <cassert>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace s3cpp::aws::iam::_internal {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::tuple<std::string, std::string> canonicalize_request(std::string_view method_string,
                                                          std::string_view encoded_target_str,
                                                          std::span<const std::byte> body,
                                                          boost::beast::http::fields &headers) {
    // see https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
    // for the canonicalization scheme

    std::string ret;
    const boost::urls::url_view encoded_target = boost::urls::parse_origin_form(encoded_target_str).value();

    // HTTPMethod
    ret.append(method_string);
    ret.append("\n");

    // CanonicalURI
    ret.append(encoded_target.encoded_path());
    ret.append("\n");

    // CanonicalQueryString
    // use a map to get them in order
    std::map<std::string, boost::urls::param_pct_view, std::less<>> params;
    for (const auto &param : encoded_target.encoded_params()) {
        params.emplace(param.key, param);
    }
    for (const auto &[key, param] : params) {
        ret.append(std::format("{}={}&", static_cast<std::string_view>(param.key),
                               static_cast<std::string_view>(param.has_value ? param.value : "")));
    }
    if (!params.empty()) {
        ret.pop_back();
    }
    ret.append("\n");

    // CanonicalHeaders
    // use a map to get them in order
    std::map<std::string, decltype(headers.cbegin()), std::less<>> headers_;
    headers_.emplace("host", headers.find(boost::beast::http::field::host));

    for (auto iter = headers.cbegin(); iter != headers.cend(); iter++) {
        std::string lower = iter->name_string();
        boost::algorithm::to_lower(lower);
        if (lower.starts_with("x-amz-")) {
            headers_.emplace(lower, iter);
        }
        if (lower == "content-md5") {
            headers_.emplace(lower, iter);
        }
    }
    for (const auto &[header_name, header] : headers_) {
        std::string_view value = header->value();
        if (const auto begin = value.find_first_not_of(' '); begin != std::string_view::npos) {
            value = value.substr(begin);
        }
        if (const auto end = value.find_last_not_of(' '); end != std::string_view::npos) {
            value = value.substr(0, end + 1);
        }
        ret.append(
            // TODO: multi-value
            std::format("{}:{}\n", header_name, value));
    }
    ret.append("\n");

    // SignedHeaders
    std::string signed_headers;
    for (const auto &[header_name, header_value] : headers_) {
        signed_headers.append(std::format("{};", header_name));
    }
    signed_headers.pop_back();
    ret.append(signed_headers);
    ret.append("\n");

    // HashedPayload
    std::string_view hash_header = headers["x-amz-content-sha256"];
    if (hash_header.empty()) {
        auto hash = Botan::HashFunction::create_or_throw("SHA-256");
        hash->update(std::string_view{
            meta::safe_reinterpret_cast<const std::string_view::value_type *>(body.data()), body.size()});
        headers.set("x-amz-content-sha256", Botan::hex_encode(hash->final_stdvec(), false));
        hash_header = headers["x-amz-content-sha256"];
    }
    ret.append(hash_header);
    // TODO: support chunked

    return {std::move(ret), std::move(signed_headers)};
}

} // namespace s3cpp::aws::iam::_internal
