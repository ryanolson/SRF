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

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::channel::v2::cpo {

inline constexpr struct close_cpo  // NOLINT
{
    template <typename T>
    requires unifex::tag_invocable<close_cpo, T&> && std::same_as<unifex::tag_invoke_result_t<close_cpo, T&>, void>
                                                     auto operator()(T& x) const
             noexcept(unifex::is_nothrow_tag_invocable_v<close_cpo, T&>) -> void
    {
        return unifex::tag_invoke(*this, x);
    }
} close;  // NOLINT

}  // namespace mrc::channel::v2::cpo
