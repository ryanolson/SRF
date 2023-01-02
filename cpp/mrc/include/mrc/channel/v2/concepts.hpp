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

#pragma once

#include "mrc/channel/status.hpp"
#include "mrc/core/concepts/types.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"

#include <concepts>
#include <utility>

namespace mrc::channel::concepts {

using namespace coroutines::concepts;

template <typename T>
concept value_type = requires {
                         requires core::concepts::not_void<T>;
                         typename T::value_type;
                     };

// clang-format off
template <typename T>
concept concrete_writable_channel = requires(T t, typename T::value_type data) {
    requires value_type<T>;
    { t.async_write(std::move(data)) } -> awaiter_of<void>;
};

template <typename T>
concept concrete_readable_channel = requires(T t) {
    requires value_type<T>;
    { t.async_read() } -> awaiter_of<mrc::expected<typename T::value_type, Status>>;
};

template <typename T>
concept type_erased_writable_channel = requires(T t, typename T::value_type data) {
    requires value_type<T>;
    { t.async_write(std::move(data)) } -> awaitable_of<void>;
};

template <typename T>
concept type_erased_readable_channel = requires(T t) {
    requires value_type<T>;
    { t.async_read() } -> awaitable_of<mrc::expected<typename T::value_type, Status>>;
};

template<typename T>
concept concrete_channel = requires {
    requires value_type<T>;
    requires concrete_readable_channel<T>;
    requires concrete_writable_channel<T>;
};

template<typename T>
concept readable_channel = requires {
    requires concrete_readable_channel<T> || type_erased_readable_channel<T>;
};

template<typename T>
concept writable_channel = requires {
    requires concrete_writable_channel<T> || type_erased_writable_channel<T>;
};




}  // namespace mrc::channel::concepts
