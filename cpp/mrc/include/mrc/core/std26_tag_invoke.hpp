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

#include <concepts>
#include <type_traits>
#include <utility>

namespace std26 {

namespace __tag_invoke_fn_ns {

void tag_invoke() = delete;

struct __tag_invoke_fn
{
    template <typename _Tag, typename... _Args>
    requires requires(_Tag __tag, _Args&&... __args) { tag_invoke((_Tag &&) __tag, (_Args &&) __args...); }
    constexpr auto operator()(_Tag __tag, _Args&&... __args) const
        noexcept(noexcept(tag_invoke((_Tag &&) __tag, (_Args &&) __args...)))
            -> decltype(tag_invoke((_Tag &&) __tag, (_Args &&) __args...))
    {
        return tag_invoke((_Tag &&) __tag, (_Args &&) __args...);
    }
};

}  // namespace __tag_invoke_fn_ns

inline namespace __tag_invoke_ns {
inline constexpr __tag_invoke_fn_ns::__tag_invoke_fn tag_invoke = {};
}

template <typename _Tag, typename... _Args>
concept tag_invocable = requires(_Tag tag, _Args... args) { tag_invoke((_Tag &&) tag, (_Args &&) args...); };

template <typename _Tag, typename... _Args>
concept nothrow_tag_invocable = tag_invocable<_Tag, _Args...> && requires(_Tag tag, _Args... args) {
                                                                     {
                                                                         tag_invoke((_Tag &&) tag, (_Args &&) args...)
                                                                     } noexcept;
                                                                 };

template <typename _Tag, typename... _Args>
using tag_invoke_result = std::invoke_result<decltype(tag_invoke), _Tag, _Args...>;

template <typename _Tag, typename... _Args>
using tag_invoke_result_t = std::invoke_result_t<decltype(tag_invoke), _Tag, _Args...>;

template <auto& _Tag>
using tag_t = std::decay_t<decltype(_Tag)>;

}  // namespace std26
