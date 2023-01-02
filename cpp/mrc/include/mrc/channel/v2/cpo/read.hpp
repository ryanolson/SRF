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

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::channel::v2::cpo {

// NOLINTBEGIN(readability-identifier-naming)

inline constexpr struct async_read_cpo
{
    template <typename T>
    requires channel::v2::concepts::data_type<T> && unifex::tag_invocable<async_read_cpo, T&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<async_read_cpo, T&>,
                                              expected<typename T::data_type, Status>>
             [[nodiscard]] auto operator()(T& x) const noexcept(unifex::is_nothrow_tag_invocable_v<async_read_cpo, T&>)
                 -> decltype(auto)
    {
        return unifex::tag_invoke(*this, x);
    }
} async_read;

inline constexpr struct read_task_cpo : public async_read_cpo
{
    template <typename T>
    requires channel::v2::concepts::data_type<T> && unifex::tag_invocable<async_read_cpo, T&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<async_read_cpo, T&>,
                                              expected<typename T::data_type, Status>>
             [[nodiscard]] auto operator()(T& x) const noexcept(unifex::is_nothrow_tag_invocable_v<async_read_cpo, T&>)
                 -> coroutines::Task<expected<typename T::data_type, Status>>
    {
        co_return co_await unifex::tag_invoke(*this, x);
    }
} read_task;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::channel::v2::cpo
