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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/cpo/write.hpp"

#include <unifex/tag_invoke.hpp>

#include <concepts>
#include <type_traits>

namespace mrc::channel::v2::concepts {

// todo(ryan) - break up readable and writable concepts in their own files

template <typename T>
concept concrete_writable =
    requires {
        requires data_type<T>;
        requires coroutines::concepts::
            awaiter_of<unifex::tag_invoke_result_t<cpo::async_write_cpo, T&, typename T::data_type&&>, void>;
    };

template <typename T>
concept writable = requires {
                       requires data_type<T>;
                       requires std::is_base_of_v<IWritableChannel<typename T::data_type>, T> ||
                                    std::same_as<IWritableChannel<typename T::data_type>, T>;
                   };

}  // namespace mrc::channel::v2::concepts
