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
#include "mrc/core/concepts/types.hpp"
#include "mrc/utils/macros.hpp"

#include <gtest/gtest.h>

#include <tuple>

using namespace mrc;
using namespace mrc::core::concepts;

namespace {

template <typename T>
class Foo
{
  public:
    using data_type = T;

    Foo(T data) : m_data(std::move(data)) {}

    const T& data() const
    {
        return m_data;
    }

  private:
    T m_data;
};

template <typename T>
class Bar
{
  public:
    using data_type = T;

    Bar(T data) : m_data(std::move(data)) {}

    const T& data() const
    {
        return m_data;
    }

  private:
    T m_data;
};

template <typename T>
concept fooable = has_data_type<T> && requires(T t) {
                                          {
                                              t.data()
                                              } -> std::same_as<const typename T::data_type&>;
                                      };

template <typename T, typename DataT>
concept fooable_of = fooable<T> && has_data_type_of<T, DataT>;

template <typename T>
concept arithmetic = std::is_arithmetic_v<T>;

template <typename T, auto ConceptFn>
concept fooable_of_concept = fooable<T> && has_data_type_of_concept<T, ConceptFn>;

template <typename T>
concept fooable_of_arithmetic = fooable_of_concept<T, MRC_CONCEPT(arithmetic)>;

template <fooable_of<int> FooT>
int fooable_of_int_fn(FooT& f)
{
    auto bar = [](const int& data) { return data * 2; };
    return bar(f.data());
}

auto fooable_of_arithmetic_fn(fooable_of_arithmetic auto& f)
{
    // lambda that take any data_type that is arithmetic
    auto bar = [](arithmetic auto const& data) { return data * 2; };
    return bar(f.data());
}

}  // namespace

TEST_F(TestConcepts, Fooable)
{
    Foo<int> foo_int(21);
    Bar<float> bar_flt(1.675);
    Foo<std::string> foo_str("mrc");

    static_assert(fooable<Foo<int>>);
    static_assert(fooable<Bar<float>>);

    EXPECT_EQ(fooable_of_int_fn(foo_int), 42);
    // fooable_of_int_fn(bar_flt);
    // fooable_of_int_fn(foo_str);

    static_assert(fooable_of_arithmetic<Foo<int>>);
    static_assert(fooable_of_arithmetic<Foo<double>>);
    static_assert(!fooable_of_arithmetic<Foo<std::string>>);

    EXPECT_EQ(fooable_of_arithmetic_fn(foo_int), 42);
    EXPECT_FLOAT_EQ(fooable_of_arithmetic_fn(bar_flt), 3.14);
    // fooable_of_arithmetic_fn(foo_str);

    auto t = std::make_tuple(foo_int, bar_flt, foo_str);
    static_assert(tuple_of_concept_of<decltype(t), MRC_CONCEPT_OF(fooable_of), int, float, std::string>);
}
