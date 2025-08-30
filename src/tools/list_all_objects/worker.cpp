#include "worker.hpp"

#include "misc.hpp"
#include "s3cpp/aws/s3/client.hpp"
#include "s3cpp/aws/s3/types.hpp"
#include "s3cpp/meta.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/describe/members.hpp>
#include <boost/describe/modifiers.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_from.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/mp11/algorithm.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// tag_invoke seems to use ADL, so we can't put it in anon namespaces

namespace std {

[[maybe_unused]] static inline void
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
tag_invoke([[maybe_unused]] const boost::json::value_from_tag &tag, boost::json::value &value,
           const chrono::time_point<chrono::system_clock> &time_point) {
    value = std::format("{:%FT%T%z}", time_point);
}

} // namespace std

namespace s3cpp::aws::s3 {

[[maybe_unused]] static inline void
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
tag_invoke([[maybe_unused]] const boost::json::value_from_tag &tag, boost::json::value &value,
           const Object &object) {
    auto &json_obj = value.emplace_object();
    boost::mp11::mp_for_each<boost::describe::describe_members<Object, boost::describe::mod_public>>(
        [&](auto member) {
            // NOLINTNEXTLINE(readability-static-accessed-through-instance)
            if constexpr (meta::is_specialization_v<std::remove_cvref_t<decltype(object.*member.pointer)>,
                                                    std::optional>) {
                // most fields will be empty. omit them to make the output smaller
                if (!(object.*member.pointer).has_value()) {
                    return;
                }
            }
            std::string_view name_view = member.name;
            if (name_view.ends_with("_")) {
                name_view = name_view.substr(0, name_view.size() - 1);
            }
            json_obj[name_view] = boost::json::value_from(object.*member.pointer);
        });
}

} // namespace s3cpp::aws::s3

namespace {

template <s3cpp::tools::list_all_objects::ListObjectsApiVersion api_version>
[[nodiscard]] s3cpp::meta::crt<boost::asio::awaitable<
    std::conditional_t<api_version == s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1,
                       s3cpp::aws::s3::ListObjectsResult, s3cpp::aws::s3::ListObjectsV2Result>>>
list_one_impl(s3cpp::aws::s3::Client client, std::string bucket, std::optional<std::string> prefix,
              std::optional<std::string> continuation_token) {
    const std::string my_prefix = prefix.value_or("<no prefix>");
    struct Visitor {
        static std::string operator()(const boost::beast::error_code &error) { return error.what(); }
        static std::string operator()(const pugi::xml_parse_status &error) {
            return std::format("pugixml error {}", std::to_underlying(error));
        }
    };
    constexpr int max_retries = 5;
    for (int retries = 1; retries <= max_retries; retries++) {
        std::string errstr;
        if constexpr (api_version == s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1) {
            auto res = co_await client.list_objects(
                {.Bucket = bucket, .Marker = continuation_token, .Delimiter = "/", .Prefix = prefix});
            if (res) {
                co_return res.value();
            }
            errstr = std::visit(Visitor{}, res.error());
        } else {
            auto res = co_await client.list_objects_v2({.Bucket = bucket,
                                                        .ContinuationToken = continuation_token,
                                                        .Delimiter = "/",
                                                        .Prefix = prefix});
            if (res) {
                co_return res.value();
            }
            errstr = std::visit(Visitor{}, res.error());
        }
        std::println(std::cerr, "WARN in prefix {} {} - retry {}", my_prefix, errstr, retries);
        boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, std::chrono::seconds{1}};
        co_await timer.async_wait(boost::asio::use_awaitable);
    }
    std::println(std::cerr, "ERROR no success after {} retries on prefix {}", max_retries, my_prefix);
    if constexpr (api_version == s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1) {
        co_return s3cpp::aws::s3::ListObjectsResult{};
    } else {
        co_return s3cpp::aws::s3::ListObjectsV2Result{};
    }
}

template <s3cpp::tools::list_all_objects::ListObjectsApiVersion api_version>
[[nodiscard]] bool check_repeated_token(
    const std::conditional_t<api_version == s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1,
                             s3cpp::aws::s3::ListObjectsResult, s3cpp::aws::s3::ListObjectsV2Result>
        &result) {
    if constexpr (api_version == s3cpp::tools::list_all_objects::ListObjectsApiVersion::V1) {
        return result.NextMarker.has_value() && result.Marker.has_value() &&
               result.Marker == result.NextMarker;
    } else {
        return result.NextContinuationToken.has_value() && result.ContinuationToken.has_value() &&
               result.ContinuationToken == result.NextContinuationToken;
    }
}

} // namespace

namespace s3cpp::tools::list_all_objects {

bool Worker::is_done() { return stats->total_queue_length == 0; }

meta::crt<boost::asio::awaitable<std::optional<std::tuple<aws::s3::CommonPrefix, std::size_t>>>>
Worker::get_next_prefix() {
    const std::scoped_lock lock{*queue_mutex};
    const PrefixQueueEntry *top{};
    while (true) {
        // someone took the last work element from the queue while we were waiting on the lock
        if (prefix_queue->empty()) {
            co_return std::nullopt;
        }
        const PrefixQueueEntry &maybe_top = prefix_queue->top();
        if (maybe_top.paths.empty()) {
            prefix_queue->pop();
        } else {
            top = &maybe_top;
            break;
        }
    }
    const auto ret = std::make_tuple(std::move(top->paths.back()), top->depth);
    top->paths.pop_back();
    stats->total_queue_length--;
    co_return ret;
}

meta::crt<boost::asio::awaitable<std::variant<aws::s3::ListObjectsResult, aws::s3::ListObjectsV2Result>>>
Worker::list_one(std::optional<std::string> prefix, std::optional<std::string> continuation_token) {
    if (api_version == ListObjectsApiVersion::V1) {
        auto result =
            co_await list_one_impl<ListObjectsApiVersion::V1>(client, bucket, prefix, continuation_token);
        co_return std::variant<aws::s3::ListObjectsResult, aws::s3::ListObjectsV2Result>{result};
    } else {
        auto result =
            co_await list_one_impl<ListObjectsApiVersion::V2>(client, bucket, prefix, continuation_token);
        co_return std::variant<aws::s3::ListObjectsResult, aws::s3::ListObjectsV2Result>{result};
    }
}

meta::crt<boost::asio::awaitable<void>> Worker::process_prefix(aws::s3::CommonPrefix prefix,
                                                               std::size_t depth) {
    std::optional<std::string> continuation_token;
    while (true) {
        auto result_variant = co_await list_one(prefix.Prefix, std::move(continuation_token));

        // Extract common fields from either result type
        std::optional<std::vector<aws::s3::Object>> contents;
        std::optional<std::vector<aws::s3::CommonPrefix>> common_prefixes;

        if (std::holds_alternative<aws::s3::ListObjectsResult>(result_variant)) {
            const auto &res = std::get<aws::s3::ListObjectsResult>(result_variant);

            if (res.NextMarker.has_value() && res.Marker.has_value() && res.Marker == res.NextMarker) {
                std::println(std::cerr, "WARN prefix {} yielded repeated Marker, skipping",
                             prefix.Prefix.value_or("<empty prefix>"));
                co_return;
            }

            continuation_token = res.NextMarker;
            contents = res.Contents;
            common_prefixes = res.CommonPrefixes;
        } else {
            const auto &res = std::get<aws::s3::ListObjectsV2Result>(result_variant);

            if (res.NextContinuationToken.has_value() && res.ContinuationToken.has_value() &&
                res.ContinuationToken == res.NextContinuationToken) {
                std::println(std::cerr, "WARN prefix {} yielded repeated ContinuationToken, skipping",
                             prefix.Prefix.value_or("<empty prefix>"));
                co_return;
            }

            continuation_token = res.NextContinuationToken;
            contents = res.Contents;
            common_prefixes = res.CommonPrefixes;
        }

        write_objects(contents.value_or(std::vector<aws::s3::Object>{}));

        {
            const std::scoped_lock lock{*queue_mutex};
            stats->total_objects_found += contents.value_or(std::vector<aws::s3::Object>{}).size();

            // Only add queue entry if there are actual sub-prefixes to process
            // Otherwise, workers may terminate because total_queue_length is 0, but there are still empty
            // entries in the queue
            auto prefixes = std::move(common_prefixes).value_or(std::vector<aws::s3::CommonPrefix>{});
            if (!prefixes.empty()) {
                stats->total_queue_length += prefixes.size();
                prefix_queue->emplace(depth + 1, std::move(prefixes));
            }

            stats->total_ops++;
        }

        if (!continuation_token.has_value()) {
            break;
        }
    }
}

void Worker::write_objects(std::span<const aws::s3::Object> objects) {
    std::string string_buf;
    if (output_format == OutputFormat::PLAIN) {
        std::size_t required_size{};
        for (const auto &object : objects) {
            if (!object.Key.has_value()) {
                std::println(std::cerr, "ERROR received object without key, ETag {}",
                             object.ETag.value_or("<no ETag>"));
                continue;
            }
            required_size += object.Key->size() + 1;
        }

        string_buf.reserve(required_size);

        for (const auto &object : objects) {
            if (object.Key.has_value()) {
                string_buf += *object.Key;
                string_buf += '\n';
            }
        }
    } else {
        for (const auto &object : objects) {
            string_buf += boost::json::serialize(boost::json::value_from(object));
            string_buf += '\n';
        }
    }

    // Push the data to the write stack instead of writing directly
    if (!string_buf.empty()) {
        write_stack->push(std::move(string_buf));
    }
}

meta::crt<boost::asio::awaitable<void>> Worker::work() {
    while (true) {
        // always try to get a work item before checking for termination:
        // the termination check is opportunistic and not fully synchronized - we could immediately shut down
        // while there's still work in the queue, causing a stall
        const auto next_prefix = co_await get_next_prefix();
        if (next_prefix.has_value()) {
            co_await process_prefix(std::get<0>(*next_prefix), std::get<1>(*next_prefix));
        }

        if (is_done() || should_terminate_early()) {
            break;
        }
    }
}

bool Worker::should_terminate_early() const {
    const std::size_t current_active = stats->active_workers;
    const std::size_t target = stats->target_workers;

    // Terminate if we have more active workers than target
    return current_active > target;
}

Worker::Worker(aws::s3::Client client, std::string bucket, std::shared_ptr<Metrics> stats,
               std::shared_ptr<boost::lockfree::stack<std::string>> write_stack,
               std::shared_ptr<PrefixQueue> prefix_queue, std::shared_ptr<std::mutex> queue_mutex,
               ListObjectsApiVersion api_version, OutputFormat output_format)
    : client{std::move(client)}, bucket{std::move(bucket)}, stats{std::move(stats)},
      write_stack{std::move(write_stack)}, api_version{api_version}, prefix_queue{std::move(prefix_queue)},
      queue_mutex{std::move(queue_mutex)}, output_format{output_format} {}

} // namespace s3cpp::tools::list_all_objects
