#include "dns_cache.hpp"

#include "s3cpp/meta.hpp"
#include "session_extra.hpp"

#include <atomic>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp> // IWYU pragma: keep
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/basic_resolver_results.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/scope/scope_exit.hpp>
#include <boost/system/errc.hpp>             // IWYU pragma: keep
#include <boost/system/error_code.hpp>       // IWYU pragma: keep
#include <boost/system/generic_category.hpp> // IWYU pragma: keep
#include <boost/url/url.hpp>
#include <chrono>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

namespace s3cpp::aws::s3::_internal {

DnsCache::DnsCache(boost::urls::url endpoint) : endpoint_url(std::move(endpoint)) {
    boost::asio::io_context context;
    boost::asio::co_spawn(
        context, update(),
        [this](std::exception_ptr exception,
               std::shared_ptr<const s3cpp::aws::s3::_internal::DnsCache::Endpoints> &&val) {
            if (exception) {
                std::rethrow_exception(std::move(exception));
            }
            current_endpoints = std::move(val);
        });
    context.run();
}

[[nodiscard]] s3cpp::meta::crt<
    boost::asio::awaitable<std::shared_ptr<const s3cpp::aws::s3::_internal::DnsCache::Endpoints>>>
DnsCache::update() {
    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::ip::tcp::resolver resolver{executor};
    const std::string port_or_scheme = endpoint_url.has_port()
                                           ? endpoint_url.port()
                                           : (endpoint_url.has_scheme() ? endpoint_url.scheme() : "https");
    auto [resolve_ec, resolved_eps] = co_await resolver.async_resolve(
        endpoint_url.host(), port_or_scheme, boost::asio::as_tuple(boost::asio::use_awaitable));
    if (resolve_ec.failed()) {
        last_error = resolve_ec;
        co_return nullptr;
    }
    if (resolved_eps.empty()) {
        last_error = boost::system::error_code{boost::system::errc::host_unreachable,
                                               boost::system::generic_category()};
        co_return nullptr;
    }
    co_return std::make_shared<s3cpp::aws::s3::_internal::DnsCache::Endpoints>(
        std::chrono::system_clock::now(), std::move(resolved_eps));
}

meta::crt<boost::asio::awaitable<
    std::expected<boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>, boost::system::error_code>>>
DnsCache::get_endpoint() {
    std::shared_ptr<const DnsCache::Endpoints> endpoints;
    while (true) {
        {
            const std::shared_lock shared_lock{endpoints_mutex};
            endpoints = current_endpoints;
        }
        if (endpoints == nullptr) {
            co_return std::expected<boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>,
                                    boost::system::error_code>{std::unexpect, last_error};
        }
        if (endpoints->created < std::chrono::system_clock::now() - std::chrono::seconds{60}) {
            bool expected = false;
            // Only one coroutine should update the endpoints.
            // Others don't need to wait on a lock though, they can just continue with the old endpoints.
            if (being_updated.compare_exchange_strong(expected, true)) {
                const boost::scope::scope_exit reset_flag{[this]() { being_updated = false; }};
                auto new_endpoints = co_await update();
                {
                    const std::unique_lock lock{endpoints_mutex};
                    current_endpoints = std::move(new_endpoints);
                }
                continue;
            }
        }
        break;
    }

    co_return endpoints->endpoints;
}

} // namespace s3cpp::aws::s3::_internal
