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

#include "unifex/detail/concept_macros.hpp"

#include "mrc/core/concepts/tuple.hpp"
#include "mrc/core/concepts/types.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/input_stream.hpp"
#include "mrc/ops/concepts/output_stream.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace mrc::ops::concepts {

template <typename T>
concept has_input_type = requires { requires std::movable<typename T::input_type>; };

template <typename T, typename DataT>
concept has_input_type_of = requires {
                                requires has_input_type<T>;
                                requires std::same_as<typename T::input_type::data_type, DataT>;
                            };

template <typename T>
concept has_single_output_type = requires { requires std::movable<typename T::output_type>; };

template <typename T>
concept has_multi_output_type = requires { requires core::concepts::tuple_like<typename T::output_type>; };

template <typename T>
concept has_output_type = requires { requires has_single_output_type<T> || has_multi_output_type<T>; };

template <typename T, typename DataT>
concept has_output_type_of = requires {
                                 // we want to be able to check for a specific data type or void
                                 requires std::movable<DataT> || std::same_as<DataT, void>;
                                 requires not has_multi_output_type<T>;
                                 requires std::same_as<typename T::output_type, DataT>;
                             };

template <typename OperationT>
concept operation = requires(OperationT op) {
                        requires has_input_type<OperationT>;
                        requires has_output_type<OperationT>;
                        requires not has_input_type_of<OperationT, Tick>;

                        // require tag_invokable cpo::execute with two different input streams
                    };

template <typename OperationT>
concept source = requires(OperationT op) {
                     requires has_input_type_of<OperationT, Tick>;
                     requires has_output_type<OperationT>;
                 };

template <typename OperationT>
concept sink = requires(OperationT op) {
                   requires has_input_type<OperationT>;
                   requires std::same_as<typename OperationT::output_type, void>;
               };

template <typename OperationT>
concept operable = requires { requires source<OperationT> || operation<OperationT> || sink<OperationT>; };

template <typename OperationT>
concept concurrent_operable = requires(const OperationT& op) {
                                  requires operable<OperationT>;
                                  {
                                      op.concurrency()
                                      } -> std::unsigned_integral;
                              };

template <typename OperationT>
concept serial_operable = requires { requires operable<OperationT> and not concurrent_operable<OperationT>; };

}  // namespace mrc::ops::concepts
