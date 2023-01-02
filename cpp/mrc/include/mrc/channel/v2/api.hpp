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

#pragma once

#include "mrc/channel/status.hpp"
#include "mrc/channel/v2/concepts.hpp"
#include "mrc/core/concepts/types.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/task.hpp"

namespace mrc::channel::v2 {

using coroutines::Task;

template <std::movable T>
struct IReadableChannel
{
    using data_type = T;

    [[nodiscard]] virtual Task<expected<T, Status>> read_task() = 0;  // noexcept?
};

struct IWritableHandle
{
    virtual ~IWritableHandle() = 0;
};

template <std::movable T>
struct IWritableChannel : public IWritableHandle
{
    using data_type = T;

    [[nodiscard]] virtual Task<> write_task(T&& data) = 0;
};

template <typename T>
struct IChannel : public IReadableChannel<T>, public IWritableChannel<T>
{
    using data_type = T;

    virtual void close() = 0;
};

inline IWritableHandle::~IWritableHandle() = default;

}  // namespace mrc::channel::v2
