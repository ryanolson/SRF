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
#include "mrc/channel/v2/concepts/data_type.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/has_scheduling_types.hpp"

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::ops::cpo::scheduling_term {

// NOLINTBEGIN(readability-identifier-naming)

inline constexpr struct evaluate_cpo
{
    template <typename T>
    requires unifex::tag_invocable<evaluate_cpo, T&>
    [[nodiscard]] auto operator()(T& x) const noexcept(unifex::is_nothrow_tag_invocable_v<evaluate_cpo, T&>)
        -> decltype(auto)
    {
        return unifex::tag_invoke(*this, x);
    }
} evaluate;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::ops::cpo::scheduling_term
