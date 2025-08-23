#pragma once

#include "s3cpp/meta.hpp"
#include "session.hpp"
#include "types.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/fields.hpp> // IWYU pragma: keep
#include <boost/describe/class.hpp>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <utility>
#include <variant>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::s3 {

struct ListObjectsParameters {
    std::string Bucket;
    std::optional<std::string> Marker;
    std::optional<std::string> Delimiter;
    std::optional<std::string> EncodingType;
    std::size_t MaxKeys = 1000;
    std::optional<std::string> Prefix;

    BOOST_DESCRIBE_STRUCT(ListObjectsParameters, (),
                          (Bucket, Marker, Delimiter, EncodingType, MaxKeys, Prefix));
};

struct ListObjectsV2Parameters {
    std::string Bucket;
    std::optional<std::string> ContinuationToken;
    std::optional<std::string> Delimiter;
    std::optional<std::string> EncodingType;
    bool FetchOwner = false;
    std::size_t MaxKeys = 1000;
    std::optional<std::string> Prefix;
    std::optional<std::string> StartAfter;

    BOOST_DESCRIBE_STRUCT(ListObjectsV2Parameters, (),
                          (Bucket, ContinuationToken, Delimiter, EncodingType, FetchOwner, MaxKeys, Prefix,
                           StartAfter));
};

class Client {
private:
    std::shared_ptr<Session> session_;

public:
    [[nodiscard]] explicit Client(std::shared_ptr<Session> session) : session_{std::move(session)} {}

    [[nodiscard]] std::shared_ptr<Session> session() const { return session_; }

    [[nodiscard]] meta::crt<boost::asio::awaitable<
        std::expected<ListObjectsResult, std::variant<boost::beast::error_code, pugi::xml_parse_status>>>>
    list_objects(ListObjectsParameters parameters, boost::beast::http::fields headers = {}) const;

    [[nodiscard]] meta::crt<boost::asio::awaitable<
        std::expected<ListObjectsV2Result, std::variant<boost::beast::error_code, pugi::xml_parse_status>>>>
    list_objects_v2(ListObjectsV2Parameters parameters, boost::beast::http::fields headers = {}) const;
};

} // namespace s3cpp::aws::s3

//
#include "s3cpp/internal/macro-end.hpp"
