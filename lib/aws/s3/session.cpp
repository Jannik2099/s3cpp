#include "s3cpp/aws/s3/session.hpp"

#include "s3cpp/aws/iam/urlencode.hpp"
#include "session_extra.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/fields.hpp>  // IWYU pragma: keep
#include <boost/beast/http/message.hpp> // IWYU pragma: keep
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/span_body.hpp>
#include <boost/beast/http/string_body.hpp> // IWYU pragma: keep
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <cstddef>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace s3cpp::aws::s3 {

namespace {

constexpr auto token = boost::asio::as_tuple(boost::asio::use_awaitable);

}

Session::crt Session::method_impl(boost::beast::http::verb method, std::string_view path,
                                  bool is_path_encoded, std::string_view query,
                                  boost::beast::http::fields headers, std::span<const std::byte> body) const {
    using rtype = Session::crt::value_type;

    std::string_view encoded_path = path;
    std::string encoded_path_buf;
    if (!is_path_encoded && iam::urlencode_path_required(path)) {
        encoded_path_buf = iam::urlencode_path(path);
        encoded_path = encoded_path_buf;
    }
    std::string_view encoded_target;
    std::string encoded_target_buf;
    if (query.empty()) {
        encoded_target = encoded_path;
    } else {
        encoded_target_buf = std::format("{}?{}", encoded_path, query);
        encoded_target = encoded_target_buf;
    }

    boost::beast::http::request<boost::beast::http::span_body<const std::byte>> request{
        method, encoded_target, 11, body, std::move(headers)};
    request = _internal::prepare_request(std::move(request), *this);

    const bool is_ssl = endpoint.scheme() != "http";
    // TODO: gracefully close ssl stream in all cases
    auto prep_res = co_await _internal::prepare_stream(_internal::get_ssl_stream(is_ssl, executor, ssl_ctx),
                                                       endpoint, executor);
    if (!prep_res) {
        co_return rtype{std::unexpect, prep_res.error()};
    }
    auto stream = std::move(prep_res.value());

    boost::beast::flat_buffer buf;
    boost::beast::http::response<boost::beast::http::string_body> response;

    const auto [send_ec, send_n] = co_await std::visit(
        [&request](auto &stream_) { return boost::beast::http::async_write(stream_, request, token); },
        stream);
    if (send_ec.failed()) {
        co_return rtype{std::unexpect, send_ec};
    }

    const auto [recv_ec, recv_n] = co_await std::visit(
        [&buf, &response](auto &stream_) {
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            return boost::beast::http::async_read(stream_, buf, response, token);
        },
        stream);
    if (recv_ec.failed()) {
        co_return rtype{std::unexpect, recv_ec};
    }

    co_return response;
}

Session::crt Session::put(std::string_view path, std::span<const std::byte> data,
                          boost::beast::http::fields headers, bool is_encoded) {
    return method_impl(boost::beast::http::verb::put, path, is_encoded, {}, std::move(headers), data);
}

Session::crt Session::put(std::string_view path, std::string_view data, boost::beast::http::fields headers,
                          bool is_encoded) {
    return put(path, std::as_bytes(std::span<const char>{data.data(), data.size()}), std::move(headers),
               is_encoded);
}

Session::crt Session::get(std::string_view path, std::string_view query, boost::beast::http::fields headers,
                          bool is_path_encoded) const {
    return method_impl(boost::beast::http::verb::get, path, is_path_encoded, query, std::move(headers), {});
}

} // namespace s3cpp::aws::s3
