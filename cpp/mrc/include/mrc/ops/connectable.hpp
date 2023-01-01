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

template <concepts::connectable_channel T>
class Connectable
{
  public:
    Connectable() = default;
    Connectable(std::shared_ptr<T> channel_port) : m_channel_port(std::move(channel_port)) {}

    DELETE_COPYABILITY(Connectable);
    DELETE_MOVEABILITY(Connectable);

    bool is_connected() const
    {
        std::lock_guard lock(m_mutex);
        return bool(m_channel_port);
    }

    void connect(std::shared_ptr<T> channel_port)
    {
        std::lock_guard lock(m_mutex);
        if (m_channel_port)
        {
            throw Error::create("unable to form new channel_port when a channel_port has already been established");
        }
        m_channel_port = channel_port;
    }

    void disconnect()
    {
        std::lock_guard lock(m_mutex);
        m_channel_port.reset();
    }

    std::shared_ptr<T> transfer()
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_channel_port, nullptr);
    }

  protected:
    T& channel() noexcept
    {
        DCHECK(m_channel_port);
        return *m_channel_port;
    }

  private:
    std::shared_ptr<T> m_channel_port;
    mutable std::mutex m_mutex;
};

}  // namespace mrc::ops
