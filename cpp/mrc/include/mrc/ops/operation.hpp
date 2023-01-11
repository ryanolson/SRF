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

#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/task.hpp"

namespace mrc::ops {

using coroutines::AsyncGenerator;
using coroutines::Task;

template <std::movable T>
using Stream = AsyncGenerator<T>;  // NOLINT

template <typename T>
struct Operation
{
    using input_type = T;

    virtual ~Operation() = default;

    // creates 
    virtual Task<> execute(Stream<T>&& stream) = 0;

    // the number of operation task that will be used to perform
    // work on the incoming data stream
    virtual std::size_t concurrency() const
    {
        return 1UL;
    }

    // called once to setup any global state for the operation
    // if the operation has a concurrency greater than 1, then
    // any of the resources setup here must be thread safe
    virtual Task<> setup()
    {
        co_return;
    }

    // called once after all operation tasks have been completed
    // used to teardown the global state
    virtual Task<> teardown()
    {
        co_return;
    }
};

template <typename T>
struct StatefulOperation : public Operation<T>
{
    virtual Task<> setup()    = 0;
    virtual Task<> teardown() = 0;
};

}  // namespace mrc::ops
