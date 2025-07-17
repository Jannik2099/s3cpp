#include "session_extra.hpp"

#include "s3cpp/aws/iam/canonicalize.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/aws/iam/sign_request.hpp"
#include "s3cpp/aws/iam/string_to_sign.hpp"
#include "s3cpp/aws/scope.hpp"
#include "s3cpp/meta.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp> // IWYU pragma: keep
#include <boost/beast/http/span_body.hpp>
#include <boost/url/url.hpp>
#include <botan/hash.h>
#include <botan/hex.h>
#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <openssl/tls1.h>
#include <string>
#include <utility>
#include <variant>

namespace s3cpp::aws::s3::_internal {

namespace {
constexpr auto token = boost::asio::as_tuple(boost::asio::use_awaitable);
}

std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>>
get_ssl_stream(bool is_ssl, boost::asio::any_io_executor executor, boost::asio::ssl::context &ssl_ctx) {
    if (is_ssl) {
        return boost::asio::ssl::stream<boost::beast::tcp_stream>{executor, ssl_ctx};
    }
    return {boost::beast::tcp_stream{executor}};
}

meta::crt<boost::asio::awaitable<
    std::expected<std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>>,
                  boost::beast::error_code>>>
prepare_stream(
    std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>> stream,
    boost::urls::url endpoint, boost::asio::any_io_executor executor) {
    using rtype = std::expected<
        std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>>,
        boost::beast::error_code>;

    std::visit(
        [&](auto &stream_) {
            boost::beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds{300});
        },
        stream);

    boost::asio::ip::tcp::resolver resolver{executor};
    const std::string port_or_scheme =
        endpoint.has_port() ? endpoint.port() : (endpoint.has_scheme() ? endpoint.scheme() : "https");

    const auto [dns_ec, resolved_ep] =
        co_await resolver.async_resolve(endpoint.host(), port_or_scheme, token);
    if (dns_ec.failed()) {
        co_return rtype{std::unexpect, dns_ec};
    }

    {
        const auto [con_ec, con_ep] = co_await std::visit(
            [&](auto &stream_) {
                // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
                return boost::beast::get_lowest_layer(stream_).async_connect(resolved_ep, token);
            },
            stream);
        if (con_ec.failed()) {
            co_return rtype{std::unexpect, con_ec};
        }
    }

    {
        const bool is_ssl = stream.index() == 1;
        if (is_ssl) {
            const auto ssl_sni_ec =
                SSL_set_tlsext_host_name(std::get<1>(stream).native_handle(), endpoint.host_name().c_str());
            if (ssl_sni_ec != 1) {
                // TODO
                co_return rtype{std::unexpect};
            }
            const auto [shake_ec] =
                co_await std::get<1>(stream).async_handshake(boost::asio::ssl::stream_base::client, token);
            if (shake_ec.failed()) {
                co_return rtype{std::unexpect, shake_ec};
            }
        }
    }

    co_return rtype{std::move(stream)};
}

boost::beast::http::request<boost::beast::http::span_body<const std::byte>>
prepare_request(boost::beast::http::request<boost::beast::http::span_body<const std::byte>> request,
                const iam::Session &session) {
    request.set(boost::beast::http::field::host, session.endpoint.encoded_host_and_port());
    if (request["x-amz-content-sha256"].empty()) {
        auto hash = Botan::HashFunction::create_or_throw("SHA-256");
        hash->update(std::string_view{
            meta::safe_reinterpret_cast<const std::string_view::value_type *>(request.body().data()),
            request.body().size()});
        request.set("x-amz-content-sha256", Botan::hex_encode(hash->final_stdvec(), false));
    }
    request.content_length(request.payload_size());
    const auto now = std::chrono::system_clock::now();
    const std::string timestamp = iam::format_timestamp(now);
    request.set("x-amz-date", timestamp);
    request.set(boost::beast::http::field::accept_encoding, "identity");

    const Scope scope{.timestamp = timestamp.substr(0, 8), .region = session.region, .service = "s3"};
    const auto &[canonical, signed_headers] = s3cpp::aws::iam::canonicalize_request(request);
    const std::string sign = s3cpp::aws::iam::sign_request(session.access_key, session.secret_access_key,
                                                           canonical, signed_headers, timestamp, scope);
    request.set(boost::beast::http::field::authorization, sign);

    return request;
}

} // namespace s3cpp::aws::s3::_internal
