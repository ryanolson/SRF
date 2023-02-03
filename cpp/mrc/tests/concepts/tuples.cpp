/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "concepts.hpp"

#include "mrc/core/concepts/tuple.hpp"
#include "mrc/utils/macros.hpp"

#include <tuple>

using namespace mrc;

namespace {

struct A
{
    A()  = default;
    ~A() = default;
    DELETE_COPYABILITY(A);
    DEFAULT_MOVEABILITY(A);
};

template <typename... T>
void foo(const A& pos, T&&... args)
{}

void bar(const A& pos, const int& i, const double& d) {}

}  // namespace

TEST_F(TestConcepts, Tuples)
{
    std::tuple<int, double> t{42, 3.14};

    static_assert(core::concepts::tuple_like<decltype(t)>);

    std::apply(foo<int, double>, std::tuple_cat(std::make_tuple(A{}), t));
    std::apply(bar, std::tuple_cat(std::make_tuple(A{}), t));

    auto args = std::tuple_cat(std::make_tuple(A{}), t);
    std::apply(bar, args);

    // std::get<N>(tuple) returns a lvalue ref, so this
    static_assert(core::concepts::tuple_element_same_as<decltype(t), 0, int>);
    static_assert(core::concepts::tuple_element_same_as<decltype(t), 1, double>);

    static_assert(core::concepts::eval_concept_fn_v<MRC_CONCEPT(std::is_arithmetic_v), float>);
    static_assert(core::concepts::eval_concept_fn_v<MRC_CONCEPT_OF(std::same_as), float, float>);

    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 0, MRC_CONCEPT(std::is_arithmetic_v)>);
    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 1, MRC_CONCEPT(std::is_arithmetic_v)>);

    static_assert(core::concepts::tuple_element_like_concept_of<decltype(t), 0, MRC_CONCEPT_OF(std::same_as), int>);
    static_assert(core::concepts::tuple_element_like_concept_of<decltype(t), 1, MRC_CONCEPT_OF(std::same_as), double>);

    static_assert(core::concepts::tuple_of_concept<decltype(t), MRC_CONCEPT(std::is_arithmetic_v)>);
    static_assert(!core::concepts::tuple_of_concept<decltype(t), MRC_CONCEPT(std::is_integral_v)>);

    static_assert(core::concepts::tuple_of_concept_of<decltype(t), MRC_CONCEPT_OF(std::same_as), int, double>);

}
