#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace sre::coro::concepts {
/**
 * This concept declares a type that is required to meet the c++20 coroutine operator co_await()
 * retun type.  It requires the following three member functions:
 *      await_ready() -> bool
 *      await_suspend(std::coroutine_handle<>) -> void|bool|std::coroutine_handle<>
 *      await_resume() -> decltype(auto)
 *          Where the return type on await_resume is the requested return of the awaitable.
 */
// clang-format off
template<typename T>
concept awaiter = requires(T t, std::coroutine_handle<> c)
{
    { t.await_ready() } -> std::same_as<bool>;
    requires std::same_as<decltype(t.await_suspend(c)), void> ||
        std::same_as<decltype(t.await_suspend(c)), bool> ||
        std::same_as<decltype(t.await_suspend(c)), std::coroutine_handle<>>;
    { t.await_resume() };
};

/**
 * This concept declares a type that can be operator co_await()'ed and returns an awaiter_type.
 */
template<typename T>
concept awaitable = requires(T t)
{
    // operator co_await()
    { t.operator co_await() } -> awaiter;
};

template<typename T>
concept awaiter_void = requires(T t, std::coroutine_handle<> c)
{
    { t.await_ready() } -> std::same_as<bool>;
    requires std::same_as<decltype(t.await_suspend(c)), void> ||
        std::same_as<decltype(t.await_suspend(c)), bool> ||
        std::same_as<decltype(t.await_suspend(c)), std::coroutine_handle<>>;
    {t.await_resume()} -> std::same_as<void>;
};

template<typename T>
concept awaitable_void = requires(T t)
{
    // operator co_await()
    { t.operator co_await() } -> awaiter_void;
};

template<awaitable AwaitableT, typename = void>
struct awaitable_traits
{
};

template<awaitable AwaitableT>
static auto get_awaiter(AwaitableT&& value)
{
    return std::forward<AwaitableT>(value).operator co_await();
}

template<awaitable AwaitableT>
struct awaitable_traits<AwaitableT>
{
    using awaiter_type        = decltype(get_awaiter(std::declval<AwaitableT>()));
    using awaiter_return_type = decltype(std::declval<awaiter_type>().await_resume());
};
// clang-format on

}  // namespace sre::coro::concepts
