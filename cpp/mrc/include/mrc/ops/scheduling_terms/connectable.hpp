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
#include "mrc/core/error.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/connectable.hpp"
#include "mrc/utils/macros.hpp"

namespace mrc::ops {

template <typename T>
class AnyReadable
{
  public:
    bool is_connected() const
    {
        return bool(m_task_generator != nullptr);
    }

    template <channel::concepts::concrete_readable_channel U>
    requires std::same_as<typename U::value_type, T>
    void connect(std::shared_ptr<U> channel)
    {
        m_task_generator = [channel]() -> coroutines::Task<expected<T, channel::Status>> {
            co_return co_await channel->async_read();
        };
    }

    void disconnect()
    {
        m_task_generator = nullptr;
    }

  private:
    mutable std::mutex m_mutex;
    bool m_connected{false};
    std::function<coroutines::Task<expected<T, channel::Status>>()> m_task_generator;
};

}  // namespace mrc::ops
