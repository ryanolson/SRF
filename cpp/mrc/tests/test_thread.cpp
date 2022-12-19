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

#include "mrc/core/thread.hpp"
#include "mrc/coroutines/sync_wait.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/coroutines/thread_pool.hpp"
#include "mrc/coroutines/when_all.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace mrc;

class TestThread : public ::testing::Test
{};

TEST_F(TestThread, GetThreadID)
{
    coroutines::ThreadPool unnamed({.thread_count = 1});
    coroutines::ThreadPool main({.thread_count = 1, .description = "main"});

    LOG(INFO) << "root: " << std::this_thread::get_id();

    auto log_id = [](coroutines::ThreadPool& tp) -> coroutines::Task<std::string> {
        co_await tp.schedule();
        std::string thread_name = mrc::this_thread::get_id();
        LOG(INFO) << "thread_name: " << thread_name;
        co_return thread_name;
    };

    std::string from_main;
    std::string from_unnamed;

    auto task = [&]() -> coroutines::Task<void> {
        from_main    = co_await log_id(main);
        from_unnamed = co_await log_id(unnamed);
        co_return;
    };

    coroutines::sync_wait(task());

    VLOG(1) << mrc::this_thread::get_id();
    VLOG(1) << from_main;
    VLOG(1) << from_unnamed;

    EXPECT_TRUE(mrc::this_thread::get_id().starts_with("sys"));
    EXPECT_TRUE(from_main.starts_with("main"));
    EXPECT_TRUE(from_unnamed.starts_with("thread_pool"));

    std::string value_main    = coroutines::sync_wait(log_id(main));
    std::string value_unnamed = coroutines::sync_wait(log_id(unnamed));

    EXPECT_TRUE(value_main.starts_with("main"));
    EXPECT_TRUE(value_unnamed.starts_with("thread_pool"));
}
