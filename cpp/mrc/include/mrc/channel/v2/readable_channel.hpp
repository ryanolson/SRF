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

struct IReadableHandle
{
    virtual ~IReadableHandle() = 0;
};

inline IReadableHandle::~IReadableHandle() = default;

template <typename T>
requires core::concepts::not_void<T> && std::movable<T>
struct IReadableChannel : public IReadableHandle
{
    using value_type = T;

    ~IReadableChannel() override = default;

    [[nodiscard]] virtual Task<expected<T, Status>> async_read() = 0;
};

template <concepts::concrete_readable_channel ReadableChannelT>
class ReadableChannel : public IReadableChannel<typename ReadableChannelT::value_type>
{
  public:
    ReadableChannel(std::shared_ptr<ReadableChannelT> channel) : m_channel(std::move(channel)) {}

    [[nodiscard]] Task<expected<typename ReadableChannelT::value_type, Status>> async_read()
    {
        co_return co_await m_channel->async_read();
    }

  private:
    std::shared_ptr<ReadableChannelT> m_channel;
};

}  // namespace mrc::channel::v2
