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

#include "mrc/coroutines/scheduler.hpp"

#include <concepts>
#include <iomanip>
#include <sstream>
#include <thread>

namespace mrc::this_thread {

namespace {

std::string to_hex(std::integral auto i)
{
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(sizeof(i) * 2) << std::hex << i;
    return ss.str();
}

std::string init_thread_id()
{
    std::stringstream ss;
    const auto* const scheduler = coroutines::Scheduler::from_current_thread();

    if (scheduler == nullptr)
    {
        ss << "sys/" << to_hex(std::hash<std::thread::id>()(std::this_thread::get_id()));
    }
    else
    {
        ss << scheduler->description() << "/" << scheduler->get_thread_id();
    }

    return ss.str();
}

}  // namespace

const std::string& get_id()
{
    static thread_local std::string id = init_thread_id();
    return id;
}

}  // namespace mrc::this_thread
