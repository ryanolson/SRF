/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "mrc/core/std23_expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/ring_buffer.hpp"

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace mrc::runnable::v2 {

namespace concepts {

using namespace coroutines::concepts;

template <typename T>
concept scheduling_term = requires(T t)
{
    typename T::value_type;
    typename T::error_type;

    // explicit return_type
    requires std::same_as<typename T::return_type, std23::expected<typename T::value_type, typename T::error_type>>;

    // T must be an awaitable or must produce an awaitable when awaited, e.g. returning a Task
    // the awaitable's return type must be the same as the expected return_type
    requires(awaitable<T> && awaitable_return_type_same_as<T, typename T::return_type>) ||
        (awaitable<decltype(t.operator co_await())> &&
         awaitable_return_type_same_as<decltype(t.operator co_await()), typename T::return_type>);
};

template <typename T>
concept operator_term = requires(T t, typename T::value_type val)
{
    typename T::value_type;
    {
        t.operator co_await(std::move(val))
        } -> awaiter;
    // t.evaluate(std::move(val));
};

}  // namespace concepts

enum class CompletionType
{
};

template <typename ValueT, typename ErrorT>
struct SchedulingTerm
{
    using value_type  = ValueT;
    using error_type  = ErrorT;
    using return_type = std23::expected<value_type, error_type>;
};


class Runnable
{

};



}  // namespace mrc::runnable::v2
