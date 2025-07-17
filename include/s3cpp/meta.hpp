#pragma once

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <utility>

//
#include "s3cpp/internal/macro-begin.hpp"

namespace s3cpp::meta {

template <typename Test, template <typename...> class Ref>
struct is_specialization : public std::false_type {};
template <template <typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : public std::true_type {};
template <typename Test, template <typename...> class Ref>
constexpr inline bool is_specialization_v = is_specialization<Test, Ref>::value;

template <typename T>
concept is_aliasing_type =
    std::is_same_v<T, unsigned char> || std::is_same_v<T, char> || std::is_same_v<T, std::byte>;

template <typename T, typename U>
T safe_reinterpret_cast(U &&rhs)
    requires std::is_pointer_v<T> && std::is_pointer_v<U> &&
             is_aliasing_type<std::remove_cvref_t<std::remove_pointer_t<T>>> &&
             is_aliasing_type<std::remove_cvref_t<std::remove_pointer_t<U>>>
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<T>(std::forward<U>(rhs));
}

template <typename Awaitable> struct [[clang::coro_return_type]] crt : public Awaitable {
public:
    template <typename Arg>
        requires std::constructible_from<Awaitable, Arg> &&
                 std::derived_from<crt, std::remove_cvref_t<Arg>> &&
                 (!std::same_as<crt, std::remove_cvref_t<Arg>>)
    [[nodiscard]] explicit(false) crt(Arg &&val) : Awaitable{std::forward<Arg>(val)} {};

    template <typename... Args>
        requires std::constructible_from<Awaitable, Args...> && (sizeof...(Args) > 1)
    [[nodiscard]] explicit crt(Args... args) : Awaitable{std::forward<Args...>(args...)} {};
};

} // namespace s3cpp::meta

template <typename Awaitable, typename... Args>
struct std::coroutine_traits<s3cpp::meta::crt<Awaitable>, Args...>
    : public std::coroutine_traits<Awaitable, Args...> {};

//
#include "s3cpp/internal/macro-end.hpp"
