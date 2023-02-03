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

#include <functional>
#include <type_traits>

namespace mrc::core::concepts {

// NOLINTBEGIN(readability-identifier-naming)

/**
 * @brief Evalutes a consteval lambda for truthiness
 *
 * @tparam F - consteval lambda whose return value is convertiable to a bool; see @MRC_CONCEPT or @MRC_CONCEPT_OF
 * @tparam T - parameter pack applied to the lambda
 */
template <auto F, typename... T>
struct eval_concept_fn : std::conditional_t<F.template operator()<T...>(), std::true_type, std::false_type>
{};

/**
 * @brief Template specialiation of eval_concept_fn for a single applied type T
 */
template <auto F, typename T>
struct eval_concept_fn<F, T> : std::conditional_t<F.template operator()<T>(), std::true_type, std::false_type>
{};

template <auto F, typename... T>
inline constexpr bool eval_concept_fn_v = eval_concept_fn<F, T...>::value;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::core::concepts

/**
 * @brief Convenience macro to wrap a concept that takes a single template type and evaluates truthiness with respect to
 * the passed in type T
 *
 * @note _T is used instead of T to avoid potential conflicts at the point of use.
 */
#define MRC_CONCEPT(concrete_concept) \
    []<typename _T>() consteval       \
    {                                 \
        return concrete_concept<_T>;  \
    }

/**
 * @brief Convenience macro to wrap a concept that takes a two template types and evaluates truthiness with respect to
 * the passed in types T and U
 */
#define MRC_CONCEPT_OF(concrete_concept)     \
    []<typename _T, typename _U>() consteval \
    {                                        \
        return concrete_concept<_T, _U>;     \
    }
