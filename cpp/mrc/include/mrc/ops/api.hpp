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

#pragma once

#include "mrc/coroutines/task.hpp"

namespace mrc::ops {

// Runtime object which we will use to set on the promise of the root task
// TaskContainer which we will use start the tasks
// A State/Status object which we can use external to push the state forward
// and allow state update events to be propagated back
// a scheduling term
// either a channel(s) adaptor or a generator adaptor

class Controller;

struct IComponent
{
    virtual ~IComponent() = default;

    virtual coroutines::Task<> initialize() = 0;
    virtual coroutines::Task<> start()      = 0;
    virtual coroutines::Task<> stop()       = 0;
    virtual coroutines::Task<> complete()   = 0;
    virtual coroutines::Task<> finalize()   = 0;
};

struct IOperator
{
    virtual ~IOperator() = default;

    virtual coroutines::Task<> main(std::shared_ptr<Controller> controller) = 0;

    virtual bool is_stoppable() const noexcept = 0;
};

}  // namespace mrc::ops
