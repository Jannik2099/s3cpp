#include "s3cpp/aws/iam/urlencode.hpp"
#include "s3cpp/aws/s3/client.hpp"
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

Bucket parse_bucket(pugi::xml_node node) {
    Bucket ret;

    if (const char *BucketArn_ = node.child_value("BucketArn"); std::strlen(BucketArn_) != 0) {
        ret.BucketArn = BucketArn_;
    }

    if (const char *BucketRegion_ = node.child_value("BucketRegion"); std::strlen(BucketRegion_) != 0) {
        ret.BucketRegion = BucketRegion_;
    }

    // TODO: CreationDate

    // TODO: error handling for missing Name
    ret.Name = node.child_value("Name");
    return ret;
}

std::expected<ListAllMyBucketsResult, pugi::xml_parse_status> parse_list_buckets(std::string &body) {
    ListAllMyBucketsResult ret;
    pugi::xml_document document;
    if (const pugi::xml_parse_status status =
            document.load_buffer_inplace(body.data(), body.size(), pugi::parse_default, pugi::encoding_utf8)
                .status;
        status != pugi::xml_parse_status::status_ok) {
        return std::unexpected{status};
    }
    const pugi::xml_node &node = document.child("ListAllMyBucketsResult");
    if (node == nullptr) {
        std::println(std::cerr, "error parsing XML response\n{}", body);
        return std::unexpected{pugi::xml_parse_status::status_file_not_found};
    }

    if (node.child("Buckets") != nullptr) {
        ret.Buckets = std::vector<Bucket>{};
        for (const auto &child : node.children("Buckets")) {
            ret.Buckets.emplace_back(parse_bucket(child.child("Bucket")));
        }
    }

    // TODO: Owner

    if (const char *ContinuationToken_ = node.child_value("ContinuationToken");
        std::strlen(ContinuationToken_) != 0) {
        ret.ContinuationToken = ContinuationToken_;
    }

    if (const char *Prefix_ = node.child_value("Prefix"); std::strlen(Prefix_) != 0) {
        ret.Prefix = Prefix_;
    }

    return ret;
}

} // namespace

meta::crt<boost::asio::awaitable<
    std::expected<ListAllMyBucketsResult, std::variant<boost::beast::error_code, pugi::xml_parse_status>>>>
Client::list_buckets(ListBucketsParameters parameters, boost::beast::http::fields headers) const {
    std::string query;
    auto add_param = [&query](std::string_view param) {
        if (!query.empty()) {
            query.append("&");
        }
        query.append(param);
    };

    if (parameters.BucketRegion.has_value()) {
        add_param(std::format("bucket-region={}", iam::urlencode_query(parameters.BucketRegion.value())));
    }
    if (parameters.ContinuationToken.has_value()) {
        add_param(
            std::format("continuation-token={}", iam::urlencode_query(parameters.ContinuationToken.value())));
    }
    add_param(std::format("max-buckets={}", parameters.MaxBuckets));
    if (parameters.Prefix.has_value()) {
        add_param(std::format("prefix={}", iam::urlencode_query(parameters.Prefix.value())));
    }

    auto res = co_await session_->get("/", query, std::move(headers), true);
    if (res) {
        co_return parse_list_buckets(res.value().body())
            .transform_error([&query](pugi::xml_parse_status err) {
                std::println(std::cerr, "ERROR query {}", query);
                return std::variant<boost::beast::error_code, pugi::xml_parse_status>{err};
            });
    }
    co_return std::unexpected<std::variant<boost::beast::error_code, pugi::xml_parse_status>>{res.error()};
}

} // namespace s3cpp::aws::s3
