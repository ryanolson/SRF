

#pragma once

#include "mrc/coroutines/concepts/awaitable.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace mrc::ops::concepts {

using namespace coroutines::concepts;

template <typename T>
concept operable_types = requires(T t) {
                             typename T::input_type;
                             typename T::output_type;
                         };

template <typename T>
concept async_operable = operable_types<T> && requires(T t) {
                                                  {
                                                      t.evaluate(std::move(std::declval<typename T::input_type>()))
                                                      } -> awaitable_of<void>;
                                              };

template <typename T>
concept async_operable_void = operable_types<T> && requires(T t) {
                                                       requires std::same_as<typename T::input_type, void>;
                                                       {
                                                           t.evaluate()
                                                           } -> awaitable_of<void>;
                                                   };

template <typename T>
concept operable_type = requires(const T ct) {
                            requires async_operable<T> || async_operable_void<T>;

                            {
                                ct.concurrency()
                                } -> std::same_as<std::size_t>;
                        };

template <typename T>
concept stateful_operable_type =
    operable_type<T> && requires(T t) {
                            typename T::TaskContext;

                            {
                                t.setup()
                                } -> awaitable_of<void>;
                            {
                                t.teardown()
                                } -> awaitable_of<void>;

                            {
                                t.context(std::declval<int>())
                                } -> std::same_as<std::add_lvalue_reference<typename T::TaskContext>>;
                            {
                                t.make_task_context()
                                } -> std::same_as<typename T::TaskContext>;
                        };

}  // namespace mrc::ops::concepts
