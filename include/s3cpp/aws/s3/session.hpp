#pragma once

#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/meta.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/fields.hpp>      // IWYU pragma: keep
#include <boost/beast/http/message.hpp>     // IWYU pragma: keep
#include <boost/beast/http/string_body.hpp> // IWYU pragma: keep
#include <boost/beast/http/verb.hpp>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::aws::s3 {

class Session : private iam::Session {
public:
    using crt = meta::crt<boost::asio::awaitable<std::expected<
        boost::beast::http::response<boost::beast::http::string_body>, boost::beast::error_code>>>;

private:
    mutable boost::asio::ssl::context ssl_ctx{boost::asio::ssl::context::tls_client};
    // mutable boost::asio::basic_socket{};
    boost::asio::any_io_executor executor;

    [[nodiscard]] crt method_impl(boost::beast::http::verb method, std::string_view path,
                                  bool is_path_encoded, std::string_view query,
                                  boost::beast::http::fields headers,
                                  std::span<const std::byte> body [[clang::lifetimebound]]) const;

public:
    [[nodiscard]] Session(iam::Session session, boost::asio::any_io_executor executor)
        : iam::Session{std::move(session)}, executor{std::move(executor)} {}

    [[nodiscard]] [[clang::coro_wrapper]] crt get(std::string_view path, std::string_view query = "",
                                                  boost::beast::http::fields headers = {},
                                                  bool is_path_encoded = false) const;

    [[nodiscard]] [[clang::coro_wrapper]] crt put(std::string_view path,
                                                  std::span<const std::byte> data [[clang::lifetimebound]],
                                                  boost::beast::http::fields headers = {},
                                                  bool is_encoded = false);
    [[nodiscard]] [[clang::coro_wrapper]] crt put(std::string_view path,
                                                  std::string_view data [[clang::lifetimebound]],
                                                  boost::beast::http::fields headers = {},
                                                  bool is_encoded = false);
};

} // namespace s3cpp::aws::s3

//
#include "s3cpp/internal/macro-end.hpp"
