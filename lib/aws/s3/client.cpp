#include "s3cpp/aws/s3/client.hpp"

#include "s3cpp/aws/iam/urlencode.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/fields.hpp> // IWYU pragma: keep
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <print>
#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace s3cpp::aws::s3 {

namespace {

[[nodiscard]] std::expected<ListBucketResult, pugi::xml_parse_status>
parse_list_objects_v2(std::string &body) {
    // std::println("{}", body);
    ListBucketResult ret;
    pugi::xml_document document;
    if (const pugi::xml_parse_status status =
            document.load_buffer_inplace(body.data(), body.size(), pugi::parse_default, pugi::encoding_utf8)
                .status;
        status != pugi::xml_parse_status::status_ok) {
        return std::unexpected{status};
    }
    const pugi::xml_node &node = document.child("ListBucketResult");
    if (node == nullptr) {
        std::println(std::cerr, "error parsing XML response\n{}", body);
        return std::unexpected{pugi::xml_parse_status::status_file_not_found};
    }

    if (node.child("CommonPrefixes") != nullptr) {
        ret.CommonPrefixes = std::vector<CommonPrefix>{};
        for (const auto &child : node.children("CommonPrefixes")) {
            ret.CommonPrefixes->emplace_back(child.child_value("Prefix"));
        }
    }

    if (node.child("Contents") != nullptr) {
        ret.Contents = std::vector<Object>{};
        for (const auto &child : node.children("Contents")) {
            ret.Contents->emplace_back(child);
        }
    }

    if (const char *ContinuationToken_ = node.child_value("ContinuationToken");
        ContinuationToken_ != nullptr && std::strlen(ContinuationToken_) != 0) {
        ret.ContinuationToken = ContinuationToken_;
    }

    if (const char *Delimiter_ = node.child_value("Delimiter");
        Delimiter_ != nullptr && std::strlen(Delimiter_) != 0) {
        ret.Delimiter = Delimiter_;
    }

    if (const std::string_view IsTruncatedStr = node.child_value("IsTruncated"); IsTruncatedStr == "true") {
        ret.IsTruncated = true;
        const char *NextContinuationToken_ = node.child_value("NextContinuationToken");
        if (NextContinuationToken_ == nullptr || std::strlen(NextContinuationToken_) <= 0) {
            throw std::runtime_error{"missing NextContinuationToken"};
        }
        ret.NextContinuationToken = NextContinuationToken_;
    } else if (IsTruncatedStr == "false") {
        ret.IsTruncated = false;
    } else {
        throw std::runtime_error{std::format("unknown IsTruncated value {}", IsTruncatedStr)};
    }

    if (const char *Prefix_ = node.child_value("Prefix"); Prefix_ != nullptr && std::strlen(Prefix_) != 0) {
        ret.Prefix = Prefix_;
    }

    return ret;
}

} // namespace

meta::crt<boost::asio::awaitable<
    std::expected<ListBucketResult, std::variant<boost::beast::error_code, pugi::xml_parse_status>>>>
Client::list_objects_v2(ListObjectsV2Parameters parameters, boost::beast::http::fields headers) const {
    std::string query{"list-type=2"};
    if (parameters.ContinuationToken.has_value()) {
        query.append(std::format("&continuation-token={}",
                                 iam::urlencode_query(parameters.ContinuationToken.value())));
    }
    if (parameters.Delimiter.has_value()) {
        query.append(std::format("&delimiter={}", iam::urlencode_query(parameters.Delimiter.value())));
    }
    if (parameters.EncodingType.has_value()) {
        query.append(std::format("&encoding-type={}", parameters.EncodingType.value()));
    }
    if (parameters.FetchOwner) {
        query.append("&fetch-owner=true");
    }
    if (parameters.Prefix.has_value()) {
        if (parameters.EncodingType.value_or("") == "url") {
            query.append(std::format("&prefix={}", parameters.Prefix.value()));
        } else {
            query.append(std::format("&prefix={}", iam::urlencode_query(parameters.Prefix.value())));
        }
    }
    if (parameters.StartAfter.has_value()) {
        query.append(std::format("&start-after={}", parameters.StartAfter.value()));
    }

    auto res =
        co_await session_->get(std::format("/{}", parameters.Bucket), query, std::move(headers), false, true);
    if (res) {
        co_return parse_list_objects_v2(res.value().body())
            .transform_error([&query](pugi::xml_parse_status err) {
                std::println(std::cerr, "ERROR query {}", query);
                return std::variant<boost::beast::error_code, pugi::xml_parse_status>{err};
            });
    }
    co_return std::unexpected<std::variant<boost::beast::error_code, pugi::xml_parse_status>>{res.error()};
}

} // namespace s3cpp::aws::s3
