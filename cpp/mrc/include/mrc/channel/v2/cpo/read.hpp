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

#include "mrc/channel/v2/concepts.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/task.hpp"

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::channel::v2::cpo {

inline constexpr struct read_cpo  // NOLINT
{
    template <typename T>
    requires channel::concepts::value_type<T> && unifex::tag_invocable<read_cpo, T&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<read_cpo, T&>,
                                              expected<typename T::value_type, Status>>
             [[nodiscard]] auto operator()(T& x) const noexcept(unifex::is_nothrow_tag_invocable_v<read_cpo, T&>)
                 -> decltype(auto)
    {
        return unifex::tag_invoke(*this, x);
    }
} read;  // NOLINT

inline constexpr struct generic_read_cpo : public read_cpo  // NOLINT
{
    template <typename T>
    requires channel::concepts::value_type<T> && unifex::tag_invocable<read_cpo, T&> &&
             coroutines::concepts::awaiter_of<unifex::tag_invoke_result_t<read_cpo, T&>,
                                              expected<typename T::value_type, Status>>
             [[nodiscard]] auto operator()(T& x) const noexcept(unifex::is_nothrow_tag_invocable_v<read_cpo, T&>)
                 -> coroutines::Task<expected<typename T::value_type, Status>>
    {
        co_return co_await unifex::tag_invoke(*this, x);
    }
} generic_read;  // NOLINT

}  // namespace mrc::channel::v2::cpo
