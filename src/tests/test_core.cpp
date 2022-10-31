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

#include "srf/core/thread.hpp"
#include "srf/coro/sync_wait.hpp"
#include "srf/coro/task.hpp"
#include "srf/coro/thread_pool.hpp"
#include "srf/coro/when_all.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace srf;

class TestCore : public ::testing::Test
{};

TEST_F(TestCore, GetThreadID)
{
    coro::ThreadPool unnamed({.thread_count = 1});
    coro::ThreadPool main({.thread_count = 1, .description = "main"});

    auto log_id = [](coro::ThreadPool& tp) -> coro::Task<std::string> {
        co_await tp.schedule();
        co_return srf::this_thread::get_id();
    };

    auto from_main    = coro::sync_wait(log_id(main));
    auto from_unnamed = coro::sync_wait(log_id(unnamed));

    VLOG(1) << srf::this_thread::get_id();
    VLOG(1) << from_main;
    VLOG(1) << from_unnamed;

    EXPECT_TRUE(srf::this_thread::get_id().starts_with("sys"));
    EXPECT_TRUE(from_main.starts_with("main"));
    EXPECT_TRUE(from_unnamed.starts_with("thread_pool"));
}
