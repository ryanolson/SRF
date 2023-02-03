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

#include "mrc/core/concepts/eval.hpp"

#include <array>
#include <compare>
#include <tuple>
#include <type_traits>
#include <utility>

namespace mrc::core::concepts {

// NOLINTBEGIN(readability-identifier-naming)

template <class T, std::size_t N>
concept has_tuple_element = requires(T t) {
                                typename std::tuple_element_t<N, std::remove_const_t<T>>;
                                {
                                    std::get<N>(t)
                                    } -> std::convertible_to<const std::tuple_element_t<N, T>&>;
                            };

template <class T, std::size_t N, typename ExpectedT>
concept tuple_element_same_as = requires(T t) {
                                    requires has_tuple_element<T, N>;
                                    {
                                        std::get<N>(t)
                                        } -> std::same_as<std::add_lvalue_reference_t<ExpectedT>>;
                                };

template <class T, std::size_t N, auto F>
concept tuple_element_like_concept = requires(T t) {
                                         requires has_tuple_element<T, N>;
                                         requires eval_concept_fn_v<F, std::decay_t<decltype(get<N>(t))>>;
                                     };

template <class T, std::size_t N, auto F, typename OfT>
concept tuple_element_like_concept_of = requires(T t) {
                                            typename std::tuple_element_t<N, std::remove_const_t<T>>;
                                            requires eval_concept_fn_v<F, std::decay_t<decltype(get<N>(t))>, OfT>;
                                        };

// clang-format off
template <class T>
concept tuple_like =
    (!std::is_reference_v<T>) and
    requires(T t) {
        typename std::tuple_size<T>::type;
        requires std::derived_from<std::tuple_size<T>, std::integral_constant<std::size_t, std::tuple_size_v<T>>>;
    } and [] <std::size_t ... N> (std::index_sequence<N...>) {
        return (has_tuple_element<T, N> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>());
// clang-format on

// clang-format off
template <class T, auto ConceptFn>
concept tuple_of_concept = tuple_like<T>
 and [] <std::size_t ... N> (std::index_sequence<N...>) {
        return (tuple_element_like_concept<T, N, ConceptFn> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>());
// clang-format on

// clang-format off
template <class T, auto ConceptFn, typename... Ts>
concept tuple_of_concept_of = tuple_like<T> and (std::tuple_size_v<T> == sizeof...(Ts))
 and [] <std::size_t ... N> (std::index_sequence<N...>) {
        return (tuple_element_like_concept_of<T, N, ConceptFn, Ts> && ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>());
// clang-format on

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::core::concepts
