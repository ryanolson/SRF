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

#include <glog/logging.h>
#include <gtest/gtest.h>

#if __cplusplus >= 202002L

    #include <coro/coro.hpp>

    #include <concepts>  // IWYU pragma: keep
    #include <memory>
    #include <type_traits>
    #include <vector>

class TestCpp20 : public ::testing::Test
{};

TEST_F(TestCpp20, Task)
{
    auto double_task = [](uint64_t x) -> coro::task<uint64_t> { co_return x * 2; };

    auto double_and_add_5_task = [&](uint64_t input) -> coro::task<uint64_t> {
        auto doubled = co_await double_task(input);
        co_return doubled + 5;
    };

    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);
}

TEST_F(TestCpp20, TheadPool)
{
    coro::thread_pool tp{coro::thread_pool::options{.thread_count = 4}};
}

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

#endif
