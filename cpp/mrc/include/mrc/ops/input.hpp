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

#include "mrc/channel/v2/concepts.hpp"
#include "mrc/channel/v2/readable_channel.hpp"
#include "mrc/core/error.hpp"
#include "mrc/utils/macros.hpp"

namespace mrc::ops {

// template <typename T, channel::concepts::readable_channel ChannelT = channel::v2::IReadableChannel<T>>
// class Input
// {
//   public:
//     [[nodiscard]] auto async_read() -> decltype(auto)
//     {
//         return m_readable_channel->async_read();
//     }

//   private:
//     std::shared_ptr<ChannelT> m_readable_channel;
// };

template <typename T>
using ReadableChannelHandle = std::shared_ptr<channel::v2::IReadableChannel<T>>;  // NOLINT

template <typename T>
class Input
{
  public:
    Input() = default;
    Input(ReadableChannelHandle<T> readable_channel) : m_readable_channel(std::move(readable_channel)) {}

    DELETE_COPYABILITY(Input);
    DELETE_MOVEABILITY(Input);

    [[nodiscard]] auto async_read() -> decltype(auto)
    {
        return m_readable_channel->async_read();
    }

    bool is_connected() const
    {
        std::lock_guard lock(m_mutex);
        return bool(m_readable_channel);
    }

    void connect_channel(ReadableChannelHandle<T> readable_channel)
    {
        std::lock_guard lock(m_mutex);
        if (m_readable_channel)
        {
            throw Error::create(
                "input already has a channel connected; disconnect existing connection before trying to make a new "
                "connection");
        }
        m_readable_channel = readable_channel;
    }

    void disconnect_channel()
    {
        std::lock_guard lock(m_mutex);
        m_readable_channel.reset();
    }

    ReadableChannelHandle<T> transfer_channel()
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_readable_channel, nullptr);
    }

  private:
    ReadableChannelHandle<T> m_readable_channel;
    mutable std::mutex m_mutex;
};

}  // namespace mrc::ops
