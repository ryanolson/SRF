/**
 * SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

/**
 * Original Source: https://github.com/jbaldwin/libcoro
 * Original License: Apache License, Version 2.0; included below
 */

/**
 * Copyright 2021 Josh Baldwin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace mrc::coroutines::concepts {
/**
 * This concept declares a type that is required to meet the c++20 coroutine operator co_await()
 * retun type.  It requires the following three member functions:
 *      await_ready() -> bool
 *      await_suspend(std::coroutine_handle<>) -> void|bool|std::coroutine_handle<>
 *      await_resume() -> decltype(auto)
 *          Where the return type on await_resume is the requested return of the awaitable.
 */
template <typename T>
concept awaiter =
    requires(T t, std::coroutine_handle<> c) {
        // clang-format off
        { t.await_ready() } -> std::same_as<bool>;
        requires std::same_as<decltype(t.await_suspend(c)), void> ||
                 std::same_as<decltype(t.await_suspend(c)), bool> ||
                 std::same_as<decltype(t.await_suspend(c)), std::coroutine_handle<>>;
        { t.await_resume() };
        // clang-format on
    };

template <typename T, typename U>
concept awaiter_of = awaiter<T> && requires(T t) {
                                       // clang-format off
                                       { t.await_resume() } -> std::same_as<U>;
                                       // clang-format on
                                   };

template <typename T>
concept awaiter_void = awaiter_of<T, void>;

template <typename T>
concept has_await_operator = requires(T t) {
                                 // clang-format off
                                 { t.operator co_await() } -> awaiter;
                                 // clang-format on
                             };

template <typename T, typename U>
concept has_await_operator_of =
    requires(T t) {
        requires has_await_operator<T>;
        requires std::same_as<std::decay_t<decltype(t.operator co_await().await_resume())>, U>;
    };

template <typename AwaitableT>
concept awaitable = awaiter<AwaitableT> || has_await_operator<AwaitableT>;

template <typename AwaitableT, typename ExpectedReturnT>
concept awaitable_of = awaiter_of<AwaitableT, ExpectedReturnT> || has_await_operator_of<AwaitableT, ExpectedReturnT>;

template <typename T>
concept awaitable_void = awaitable_of<T, void>;

template <awaitable AwaitableT, typename = void>
struct awaitable_traits
{};

template <awaitable AwaitableT>
static auto get_awaiter(AwaitableT&& value)
{
    return std::forward<AwaitableT>(value).operator co_await();
}

template <awaitable AwaitableT>
struct awaitable_traits<AwaitableT>
{
    using awaiter_type        = decltype(get_awaiter(std::declval<AwaitableT>()));
    using awaiter_return_type = decltype(std::declval<awaiter_type>().await_resume());
};

}  // namespace mrc::coroutines::concepts
