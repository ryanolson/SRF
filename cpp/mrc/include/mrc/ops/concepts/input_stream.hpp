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

template <typename InputStreamT>
concept input_stream_iterable = core::concepts::has_data_type<InputStreamT> &&
                                requires(InputStreamT& stream) {
                                    {
                                        stream.next()
                                        } -> coroutines::concepts::awaitable;

                                    {
                                        stream.operator bool()
                                        } -> std::same_as<bool>;
                                };
template <typename InputStreamT>
concept input_stream =
    requires(InputStreamT& stream) {
        requires input_stream_iterable<InputStreamT>;

        // non-const
        {
            stream.data()
            } -> std::same_as<std::add_lvalue_reference_t<typename InputStreamT::data_type>>;
    } || requires(const InputStreamT& stream) {
             requires input_stream_iterable<InputStreamT>;

             // const
             {
                 stream.data()
                 } -> std::same_as<std::add_lvalue_reference_t<const typename InputStreamT::data_type>>;
         };

template <typename InputStreamT, typename DataT>
concept input_stream_of = input_stream<InputStreamT> && std::same_as<typename InputStreamT::data_type, DataT>;

}  // namespace mrc::ops::concepts
