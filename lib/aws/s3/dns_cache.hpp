#pragma once

#include "s3cpp/meta.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/basic_resolver_results.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp> // IWYU pragma: keep
#include <boost/url/url.hpp>
#include <chrono>
#include <expected>
#include <memory>
#include <shared_mutex>

namespace s3cpp::aws::s3::_internal {

class DnsCache {
public:
    struct Endpoints {
        std::chrono::time_point<std::chrono::system_clock> created;
        boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp> endpoints;
    };

private:
    boost::urls::url endpoint_url;
    std::shared_mutex endpoints_mutex;
    std::shared_ptr<const Endpoints> current_endpoints;
    std::atomic<bool> being_updated = false;
    std::atomic<boost::system::error_code> last_error;

    [[nodiscard]] s3cpp::meta::crt<
        boost::asio::awaitable<std::shared_ptr<const s3cpp::aws::s3::_internal::DnsCache::Endpoints>>>
    update();

public:
    DnsCache(boost::urls::url endpoint);

    [[nodiscard]] meta::crt<boost::asio::awaitable<std::expected<
        boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>, boost::system::error_code>>>
    get_endpoint();
};

} // namespace s3cpp::aws::s3::_internal
