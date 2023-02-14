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

#include "unifex/detail/concept_macros.hpp"

#include "mrc/core/concepts/types.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/ops/component.hpp"
#include "mrc/ops/cpo/inputs.hpp"

#include <unifex/tag_invoke.hpp>

#include <stop_token>

namespace mrc::ops::concepts {

using namespace coroutines::concepts;

template <typename T>
concept scheduling_term = requires(T t) {
                              requires core::concepts::has_data_type<T>;
                              requires unifex::tag_invocable<cpo::make_input_stream_cpo, T&, std::stop_token&&>;
                              requires std::is_base_of_v<Component, T>;
                          };

template <typename T, typename DataT>
concept scheduling_term_of = requires {
                                 requires scheduling_term<T>;
                                 requires std::same_as<typename T::dat_type, DataT>;
                             };

// template <typename T>
// concept schedulable =
//     requires {
//         typename T::data_type;
//         typename T::error_type;

//         // explicit return_type
//         requires std::same_as<typename T::return_type, expected<typename T::data_type, typename T::error_type>>;

//         // T must be an awaitable with the expected return_type
//         requires awaitable_of<unifex::tag_invoke_result_t<cpo::scheduling_term::evaluate_cpo, T&>,
//                               typename T::return_type>;
//     };

}  // namespace mrc::ops::concepts
