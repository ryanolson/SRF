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

#include "mrc/channel/v2/concepts/data_type.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/task.hpp"

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::channel::v2::cpo {

// NOLINTBEGIN(readability-identifier-naming)

inline constexpr struct async_write_cpo
{
    template <typename T>
    requires concepts::has_data_type<T> && unifex::tag_invocable<async_write_cpo, T&, typename T::data_type&&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<async_write_cpo, T&, typename T::data_type&&>,
                                              void>
             [[nodiscard]] auto operator()(T& x, typename T::data_type&& data) const
             noexcept(unifex::is_nothrow_tag_invocable_v<async_write_cpo, T&, typename T::data_type&&>)
                 -> decltype(auto)
    {
        return unifex::tag_invoke(*this, x, std::move(data));
    }
} async_write;

inline constexpr struct write_task_cpo : public async_write_cpo
{
    template <typename T>
    requires concepts::has_data_type<T> && unifex::tag_invocable<async_write_cpo, T&, typename T::data_type&&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<async_write_cpo, T&, typename T::data_type&&>,
                                              void>
             [[nodiscard]] auto operator()(T& x, typename T::data_type&& data) const
             noexcept(unifex::is_nothrow_tag_invocable_v<async_write_cpo, T&, typename T::data_type&&>)
                 -> coroutines::Task<>
    {
        co_await unifex::tag_invoke(*this, x, std::move(data));
        co_return;
    }
} write_task;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::channel::v2::cpo
