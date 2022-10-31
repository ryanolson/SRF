#pragma once

#include "srf/coro/concepts/awaitable.hpp"

#include <concepts>

namespace srf::coro::concepts {

// clang-format off
template<typename T, typename ReturnT>
concept promise = requires(T t)
{
    { t.get_return_object() } -> std::convertible_to<std::coroutine_handle<>>;
    { t.initial_suspend() } -> awaiter;
    { t.final_suspend() } -> awaiter;
    { t.yield_value() } -> awaitable;
}
&& requires(T t, ReturnT return_value)
{
    requires std::same_as<decltype(t.return_void()), void> ||
             std::same_as<decltype(t.return_value(return_value)), void> ||
             requires { t.yield_value(return_value); };
};
// clang-format on

}  // namespace srf::coro::concepts
