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
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/input_stream.hpp"
#include "mrc/ops/concepts/output_stream.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace mrc::ops::concepts {

template <typename T>
concept has_input_type = requires { requires input_stream<typename T::input_type>; };

template <typename T, typename DataT>
concept has_input_type_of = requires {
                                requires input_stream<typename T::input_type>;
                                requires std::same_as<typename T::input_type::data_type, DataT>;
                            };

template <typename T>
concept has_output_type = requires { requires output_stream<typename T::output_type>; };

template <typename OperationT>
concept operation = requires(OperationT op) {
                        requires has_input_type<OperationT>;
                        requires has_output_type<OperationT>;

                        {
                            op.execute(std::declval<std::add_lvalue_reference_t<typename OperationT::input_type>>(),
                                       std::declval<std::add_lvalue_reference_t<typename OperationT::output_type>>())
                            } -> std::same_as<coroutines::Task<>>;
                    };

template <typename OperationT>
concept source = requires(OperationT op) {
                     requires has_input_type<OperationT>;
                     requires has_output_type<OperationT>;

                     {
                         op.execute(std::declval<std::add_lvalue_reference_t<typename OperationT::input_type>>(),
                                    std::declval<std::add_lvalue_reference_t<typename OperationT::output_type>>())
                         } -> std::same_as<coroutines::Task<>>;
                 };

template <typename OperationT>
concept sink = requires(OperationT op) {
                   requires has_input_type<OperationT>;
                   requires std::same_as<typename OperationT::output_type, void>;

                   {
                       op.execute(std::declval<std::add_lvalue_reference_t<typename OperationT::input_type>>())
                       } -> std::same_as<coroutines::Task<>>;
               };

template <typename OperationT>
concept operable = requires { requires source<OperationT> || operation<OperationT> || sink<OperationT>; };

namespace next
{




}

}  // namespace mrc::ops::concepts
