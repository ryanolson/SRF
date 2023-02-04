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

namespace test::concepts {

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
    auto bar = [](const int& data) {
        return data * 2;
    };
    return bar(f.data());
}

template <fooable_of_arithmetic FooT>
auto fooable_of_arithmetic_fn(FooT& f)
{
    // lambda that take any data_type that is arithmetic
    auto bar = [](arithmetic auto const& data) {
        return data * 2;
    };
    return bar(f.data());
}

TEST_F(TestConcepts, Fooable)
{
    Foo<int> foo_int(21);
    Bar<float> bar_flt(1.57);
    Foo<std::string> foo_str("mrc");

    static_assert(fooable<Foo<int>>);
    static_assert(fooable<Bar<float>>);

    EXPECT_EQ(fooable_of_int_fn(foo_int), 42);
    // fooable_of_int_fn(bar_flt);
    // fooable_of_int_fn(foo_str);

    static_assert(fooable_of_arithmetic<Foo<int>>);
    static_assert(fooable_of_arithmetic<Foo<double>>);
    static_assert(!fooable_of_arithmetic<Foo<std::string>>);

#if !defined(__GNUC__)
    // these tests work with clang-15.0.6
    // the following tests fail on gcc 11.2, 12.1 and 12.2, but pass on gcc trunk
    EXPECT_EQ(fooable_of_arithmetic_fn(foo_int), 42);
    EXPECT_FLOAT_EQ(fooable_of_arithmetic_fn(bar_flt), 3.14);
    // fooable_of_arithmetic_fn(foo_str);
#endif

    auto t = std::make_tuple(foo_int, bar_flt, foo_str);
    static_assert(tuple_of_concept_of<decltype(t), MRC_CONCEPT_OF(fooable_of), int, float, std::string>);

    auto fn = [](const int& i, const float& f, const std::string& a) {
        std::stringstream ss;
        ss << "inputs: " << i << ", " << f << ", " << a;
        return ss.str();
    };

    auto foo_tuple_fn =
        [fn](tuple_of_concept_of<MRC_CONCEPT_OF(fooable_of), int, float, std::string> auto& fooable_tuple)
        -> std::string {
        auto data_tuple = std::apply(
            [](auto&... args) {
                return std::make_tuple(args.data()...);
            },
            fooable_tuple);
        return std::apply(fn, data_tuple);
    };

#if !defined(__clang__)
    // this line causes a crash with clang-15.0.6 and clang trunk
    // https://godbolt.org/z/P8Kce1fcW
    auto str = foo_tuple_fn(t);
#endif
}

}  // namespace test::concepts
