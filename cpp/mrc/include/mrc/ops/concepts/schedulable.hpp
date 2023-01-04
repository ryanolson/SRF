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

#include "mrc/core/expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/ops/cpo/scheduling_term.hpp"

#include <unifex/tag_invoke.hpp>

namespace mrc::ops::concepts {

using namespace coroutines::concepts;

template <typename T>
concept schedulable =
    requires {
        typename T::data_type;
        typename T::error_type;

        // explicit return_type
        requires std::same_as<typename T::return_type, expected<typename T::data_type, typename T::error_type>>;

        // requires std::same_as<decltype(t.await_suspend(c)), void> ||
        //              std::same_as<decltype(t.await_suspend(c)), bool> ||
        //              std::same_as<decltype(t.await_suspend(c)), std::coroutine_handle<>>;

        // T must be an awaitable with the expected return_type
        // requires awaitable_of<T, typename T::return_type> || awaiter_of<T, typename T::return_type>;

        requires awaitable_of<unifex::tag_invoke_result_t<cpo::scheduling_term::evaluate_cpo, T&>,
                              typename T::return_type> ||
                     awaiter_of<unifex::tag_invoke_result_t<cpo::scheduling_term::evaluate_cpo, T&>,
                                typename T::return_type>;
    };

}  // namespace mrc::ops::concepts
