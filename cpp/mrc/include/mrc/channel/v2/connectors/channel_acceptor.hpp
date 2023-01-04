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

#include "mrc/channel/v2/concepts/readable.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/core/error.hpp"
#include "mrc/utils/macros.hpp"

namespace mrc::channel::v2 {

/**
 * @brief Provides the acceptor half of the connectable concept. This class is not thread-safe. It is designed to be
 * inherited
 *
 * @tparam ChannelT
 */

template <typename ChannelT>
requires concepts::readable<ChannelT> || concepts::writable<ChannelT>
class ChannelAcceptor
{
  public:
    ChannelAcceptor() = default;
    ChannelAcceptor(std::shared_ptr<ChannelT>);

    DELETE_COPYABILITY(ChannelAcceptor);
    DELETE_MOVEABILITY(ChannelAcceptor);

    bool is_connected() const noexcept
    {
        return m_channel;
    }

    void connect_channel(std::shared_ptr<ChannelT> channel)
    {
        if (m_channel)
        {
            throw Error::create(
                "connection error: channel already connected; disconnect existing connections before trying to make a "
                "new connection");
        }
        m_channel = channel;
    }

    void disconnect_channel() noexcept
    {
        m_channel.reset();
    }

  protected:
    /**
     * @brief Access the backing channel. It is a requirement that the caller has checked that a channel is connected
     * before trying to access it.
     *
     * @return ChannelT&
     */
    inline ChannelT& channel() noexcept
    {
        return *m_channel;
    }

  private:
    std::shared_ptr<ChannelT> m_channel;
};

}  // namespace mrc::channel::v2
