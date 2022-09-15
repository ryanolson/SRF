#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace sre::coro::concepts {

// clang-format off
template<typename T>
concept const_buffer = requires(const T t)
{
    { t.empty() } -> std::same_as<bool>;
    { t.data() } -> std::same_as<const char*>;
    { t.size() } -> std::same_as<std::size_t>;
};

template<typename T>
concept mutable_buffer = requires(T t)
{
    { t.empty() } -> std::same_as<bool>;
    { t.data() } -> std::same_as<char*>;
    { t.size() } -> std::same_as<std::size_t>;
};
// clang-format on

}  // namespace sre::coro::concepts
