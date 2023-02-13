/**
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/coroutines/task.hpp"
#include "mrc/ops/api.hpp"

namespace mrc::ops {

struct Component : IComponent
{
    coroutines::Task<> initialize() override
    {
        co_return;
    }
    coroutines::Task<> start() override
    {
        co_return;
    }
    coroutines::Task<> stop() override
    {
        co_return;
    }
    coroutines::Task<> complete() override
    {
        co_return;
    }
    coroutines::Task<> finalize() override
    {
        co_return;
    }
};

}  // namespace mrc::ops
