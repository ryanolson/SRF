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

#include "srf/concepts/invocable.hpp"

#include <optional>
#if __cplusplus >= 202002L

    #include "srf/core/error.hpp"
    #include "srf/core/optional_ref.hpp"

    #include <glog/logging.h>
    #include <gtest/gtest.h>

    #include <concepts>  // IWYU pragma: keep
    #include <functional>
    #include <memory>
    #include <type_traits>
    #include <vector>

using namespace srf;

class TestCpp20 : public ::testing::Test
{};

template <typename T>
concept vector_like = requires(T t)
{
    t.begin();
    t.reserve(1);
    t.data();
};

static_assert(vector_like<std::vector<int>>);
static_assert(!vector_like<int>);

template <typename T>
concept smart_ptr_like = requires(T t)
{
    t.operator*();
    t.operator->();
    t.release();
    t.reset();
    typename T::element_type;

    requires !std::copyable<T>;

    // clang-format off
    { t.operator->() } -> std::same_as<typename std::add_pointer<typename T::element_type>::type>;
    { *t } -> std::same_as<typename std::add_lvalue_reference<typename T::element_type>::type>;
    // clang-format on
};

static_assert(smart_ptr_like<std::unique_ptr<int>>);
static_assert(!smart_ptr_like<std::shared_ptr<int>>);
static_assert(!smart_ptr_like<int>);

struct FooClass
{
    int foo;

    const int& get_foo() const
    {
        return foo;
    }
};

TEST_F(TestCpp20, OptionalRef)
{
    OptionalRef<int> int_ref;

    EXPECT_FALSE(int_ref);
    EXPECT_ANY_THROW(*int_ref);

    bool was_called = false;

    // std::function<void(int&)> not_called = [&](int& i) { was_called = true; };
    // auto status = int_ref.and_then(not_called, [&] { is_null = true; });

    auto status       = int_ref.and_then([&](int& i) { was_called = true; });
    auto status_const = int_ref.and_then([&](const int& i) { was_called = true; });

    EXPECT_FALSE(status);
    EXPECT_FALSE(status_const);
    EXPECT_EQ(status.error().code(), ErrorCode::NullOptional);
    EXPECT_EQ(status_const.error().code(), ErrorCode::NullOptional);
    EXPECT_FALSE(was_called);

    int ans = 42;
    int_ref.reset(ans);

    EXPECT_TRUE(int_ref);
    EXPECT_EQ(*int_ref, 42);

    auto expected_str = int_ref.and_then([](int& i) mutable -> std::string {
        std::stringstream ss;
        ss << "ans: " << i;
        return ss.str();
    });

    EXPECT_TRUE(expected_str);
    EXPECT_TRUE(*expected_str == "ans: 42");

    auto expected_str_const = int_ref.and_then([](const int& i) mutable -> std::string {
        std::stringstream ss;
        ss << "ans_const: " << i;
        return ss.str();
    });

    EXPECT_TRUE(expected_str_const);
    EXPECT_TRUE(*expected_str_const == "ans_const: 42");

    OptionalRef<int> no(std::nullopt);
    OptionalRef<int> ref(ans);

    FooClass a{.foo = 42};

    // std::optional<std::reference_wrapper<FooClass>> orw(a);
    OptionalRef ora(a);
    EXPECT_TRUE(ora);
    EXPECT_EQ(ora->foo, 42);
    EXPECT_EQ(ora->get_foo(), 42);

    ora.reset();

    EXPECT_FALSE(ora);
    EXPECT_ANY_THROW(ora->get_foo());
}

#endif
