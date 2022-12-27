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
#include "mrc/core/std23_expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"

#include <concepts>
#include <utility>

namespace mrc::channel::concepts {

using namespace coroutines::concepts;

// clang-format off
template <typename T>
concept concrete_writable_channel = requires(T t, typename T::value_type data) {
    typename T::value_type;  // not void
    { t.write(std::move(data)) } -> awaiter_of<void>;
};

template <typename T>
concept concrete_readable_channel = requires(T t) {
    typename T::value_type;  // not void
    { t.read() } -> awaiter_of<std23::expected<typename T::value_type, Status>>;
};

template <typename T>
concept writable_channel = requires(T t, typename T::value_type data) {
    typename T::value_type;  // not void
    { t.write(std::move(data)) } -> awaitable_of<void>;
};

template <typename T>
concept readable_channel = requires(T t) {
    typename T::value_type;  // not void
    { t.read() } -> awaitable_of<std23::expected<typename T::value_type, Status>>;
};


}  // namespace mrc::channel::concepts
