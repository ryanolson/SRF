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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/cpo/read.hpp"
#include "mrc/core/concepts/types.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"

#include <unifex/tag_invoke.hpp>

#include <concepts>
#include <type_traits>

namespace mrc::channel::v2::concepts {

// todo(ryan) - break up readable and writable concepts in their own files

using namespace core::concepts;
using namespace coroutines::concepts;

template <typename T>
concept concrete_readable = requires(T t) {
                                requires has_data_type<T>;
                                {
                                    cpo::async_read(t)
                                    } -> awaiter_of<expected<typename T::data_type, Status>>;
                            };

template <typename ChannelT, typename DataT>
concept concrete_readable_of = concrete_readable<ChannelT> && std::same_as<typename ChannelT::data_type, DataT>;

template <typename T>
concept readable = requires {
                       requires has_data_type<T>;
                       requires std::is_base_of_v<IReadableChannel<typename T::data_type>, T> ||
                                    std::same_as<IReadableChannel<typename T::data_type>, T>;
                   };

template <typename ChannelT, typename DataT>
concept readable_of = readable<ChannelT> && std::same_as<typename ChannelT::data_type, DataT>;

}  // namespace mrc::channel::v2::concepts
