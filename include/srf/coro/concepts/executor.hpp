#pragma once

#include "srf/coro/concepts/awaitable.hpp"

#include <concepts>
#include <coroutine>

namespace srf::coro::concepts {

// clang-format off
template<typename T>
concept executor = requires(T t, std::coroutine_handle<> c)
{
    { t.schedule() } -> coro::concepts::awaiter;
    { t.yield() } -> coro::concepts::awaiter;
    { t.resume(c) } -> std::same_as<void>;
};
// clang-format on

}  // namespace srf::coro::concepts
