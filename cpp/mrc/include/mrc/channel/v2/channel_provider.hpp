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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/concepts/channel.hpp"

namespace mrc::channel::v2 {

template <concepts::data_type ChannelT>
class ChannelProvider
{
  public:
    using data_type = typename ChannelT::data_type;

    class WritableChannel final : public IWritableChannel<data_type>
    {
      public:
        WritableChannel(std::shared_ptr<ChannelT> channel) : m_channel(std::move(channel)) {}
        ~WritableChannel() final = default;

        [[nodiscard]] inline auto write_task(data_type&& data) -> coroutines::Task<> final
        {
            return m_channel->write_task(std::move(data));
        }

      private:
        friend auto tag_invoke(unifex::tag_t<cpo::async_write> _, WritableChannel& t, data_type&& data) noexcept
            -> decltype(auto)
        requires concepts::concrete_writable<ChannelT>
        {
            return cpo::async_write(*(t.m_channel), std::move(data));
        }

        const std::shared_ptr<ChannelT> m_channel;
    };

    class ReadableChannel final : public IReadableChannel<data_type>
    {
      public:
        ReadableChannel(std::shared_ptr<ChannelT> channel) : m_channel(std::move(channel)) {}
        ~ReadableChannel() final = default;

        [[nodiscard]] inline auto read_task() -> coroutines::Task<expected<data_type, Status>> final
        {
            return m_channel->read_task();
        }

      private:
        friend auto tag_invoke(unifex::tag_t<cpo::async_read> _, ReadableChannel& t) noexcept -> decltype(auto)
        requires concepts::concrete_readable<ChannelT>
        {
            return cpo::async_read(*(t.m_channel));
        }

        const std::shared_ptr<ChannelT> m_channel;
    };

    ChannelProvider(std::unique_ptr<ChannelT> channel) : m_channel(std::move(channel))
    {
        m_writable_channel = std::make_shared<WritableChannel>(m_channel);
    }
    ~ChannelProvider() = default;

    std::shared_ptr<ReadableChannel> readable_channel()
    {
        std::shared_ptr<ReadableChannel> readable = m_readable_channel.lock();
        if (readable == nullptr)
        {
            auto channel = m_channel;
            readable     = {new ReadableChannel(channel), [channel](ReadableChannel* readable) {
                            delete readable;
                            channel->close();
                        }};

            m_readable_channel   = readable;
            m_readable_persisent = readable;
        }
        return readable;
    }

    std::shared_ptr<WritableChannel> writable_channel()
    {
        return m_writable_channel;
    }

  private:
    std::shared_ptr<ChannelT> m_channel;
    std::shared_ptr<WritableChannel> m_writable_channel;
    std::shared_ptr<ReadableChannel> m_readable_persisent;
    std::weak_ptr<ReadableChannel> m_readable_channel;
};

template <concepts::data_type ChannelT>
auto make_channel_provider(std::unique_ptr<ChannelT> channel)
{
    return ChannelProvider<ChannelT>(std::move(channel));
}

}  // namespace mrc::channel::v2
