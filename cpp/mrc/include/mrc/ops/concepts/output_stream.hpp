/**
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/core/concepts/types.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace mrc::ops::concepts {

template <typename OutputStreamT>
concept output_stream = core::concepts::has_data_type<OutputStreamT> &&
                        requires(OutputStreamT& stream, typename OutputStreamT::data_type data) {
                            {
                                stream.emit(data)
                                } -> coroutines::concepts::awaitable_of<void>;

                            {
                                stream.emit(std::move(data))
                                } -> coroutines::concepts::awaitable_of<void>;
                        };

template <typename OutputStreamT, typename DataT>
concept output_stream_of = output_stream<OutputStreamT> && std::same_as<typename OutputStreamT::data_type, DataT>;

}  // namespace mrc::ops::concepts
