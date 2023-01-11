

#pragma once

#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace mrc::ops::concepts {

using namespace coroutines::concepts;

template <typename T>
concept operable_types = requires(T t) { requires std::movable<typename T::input_type>; };

template <typename T>
concept operable = operable_types<T> && requires(T t, coroutines::AsyncGenerator<typename T::input_type> inputs) {
                                            {
                                                t.main(std::move(inputs))
                                                } -> awaitable_of<void>;

                                            {
                                                t.concurrency()
                                                } -> std::same_as<std::size_t>;

                                            {
                                                t.setup()
                                                } -> awaitable_of<void>;
                                            {
                                                t.teardown()
                                                } -> awaitable_of<void>;
                                        };

template <typename T>
concept stateful_operable =
    operable<T> && requires(T t) {
                       typename T::TaskContext;

                       {
                           t.context(std::declval<int>())
                           } -> std::same_as<std::add_lvalue_reference<typename T::TaskContext>>;
                       {
                           t.make_task_context()
                           } -> std::same_as<typename T::TaskContext>;
                   };

}  // namespace mrc::ops::concepts
