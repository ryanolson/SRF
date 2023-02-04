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

#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/ops/concepts/input_stream.hpp"

#include <unifex/tag_invoke.hpp>

#include <stop_token>
#include <utility>

namespace mrc::ops::cpo {

// NOLINTBEGIN(readability-identifier-naming)

inline constexpr struct make_input_stream_cpo
{
    template <typename T>
    requires core::concepts::has_data_type<T> and unifex::tag_invocable<make_input_stream_cpo, T&, std::stop_token> and
             concepts::input_stream_of<unifex::tag_invoke_result_t<make_input_stream_cpo, T&, std::stop_token>,
                                       typename T::data_type>
             [[nodiscard]] auto operator()(T& x, std::stop_token stop_token) const
             noexcept(unifex::is_nothrow_tag_invocable_v<make_input_stream_cpo, T&, std::stop_token>) -> decltype(auto)
    {
        return unifex::tag_invoke(*this, x, std::move(stop_token));
    }
} make_input_stream;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::ops::cpo
