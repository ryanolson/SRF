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

#include "mrc/channel/v2/channel.hpp"
#include "mrc/coroutines/event.hpp"
#include "mrc/coroutines/ring_buffer.hpp"
#include "mrc/coroutines/schedule_policy.hpp"

namespace mrc::channel::v2 {

template <typename T>
class BufferedChannel final : public IChannel<T>
{
  public:
    struct Options
    {
        std::size_t capacity{2};
    };

    explicit BufferedChannel(Options options = {.capacity = 2}) :
      m_queue({.capacity     = options.capacity,
               .write_policy = coroutines::SchedulePolicy::Reschedule,
               .read_policy  = coroutines::SchedulePolicy::Immediate})
    {}

    ~BufferedChannel() final = default;

    BufferedChannel(const BufferedChannel&)            = delete;
    BufferedChannel(BufferedChannel&&)                 = delete;
    BufferedChannel& operator=(const BufferedChannel&) = delete;
    BufferedChannel& operator=(BufferedChannel&&)      = delete;

    [[nodiscard]] Task<> async_write(T&& data) final
    {
        co_return co_await m_queue.write(std::move(data));
    }

    [[nodiscard]] Task<std23::expected<T, channel::Status>> async_read() final
    {
        co_return co_await m_queue.read();
    }

    [[nodiscard]] Task<> close() final
    {
        co_await m_queue.close();
        co_return;
    }

  private:
    coroutines::RingBuffer<T> m_queue;
};

}  // namespace mrc::channel::v2
