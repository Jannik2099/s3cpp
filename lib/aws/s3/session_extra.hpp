#include "dns_cache.hpp"
#include "s3cpp/aws/iam/session.hpp"
#include "s3cpp/meta.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <boost/beast/http/span_body.hpp>
#pragma GCC diagnostic pop

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp> // IWYU pragma: keep
#include <boost/url/url.hpp>
#include <cstddef>
#include <expected>
#include <memory>
#include <variant>

namespace s3cpp::aws::s3::_internal {

[[nodiscard]] std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>>
get_ssl_stream(bool is_ssl, boost::asio::any_io_executor executor, boost::asio::ssl::context &ssl_ctx);

[[nodiscard]] meta::crt<boost::asio::awaitable<
    std::expected<std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>>,
                  boost::beast::error_code>>>
prepare_stream(
    std::variant<boost::beast::tcp_stream, boost::asio::ssl::stream<boost::beast::tcp_stream>> stream
    [[clang::lifetimebound]],
    boost::urls::url endpoint, std::shared_ptr<DnsCache> dns_cache);

[[nodiscard]]
boost::beast::http::request<boost::beast::http::span_body<const std::byte>>
prepare_request(boost::beast::http::request<boost::beast::http::span_body<const std::byte>> request
                [[clang::lifetimebound]],
                const iam::Session &session);

} // namespace s3cpp::aws::s3::_internal
