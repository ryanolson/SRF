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

/**
 * @brief ChannelProvider takes ownership of a Channel object and provides reference counted Readable/WritableChannel
 * objects which conform to the readable and writable concepts. WritableChannels will close the channe when all
 * WritableChannels have been released; this is a soft close of the channel allowing readers to drain the channel.
 * ReadableChannels will issue a hard close (kill) on the backing channel when the last ReaderChannel is released; this
 * ensures than any writer is resumed as all the readers have gone away.
 *
 * In a normal operating sequence, one should see the soft close logging message first, then hard close logging message.
 * Until the kill method is added to Channel API, any hard close logging message without a matching soft close message,
 * could indicate that the Channel is holding the coroutine handles for 1 or more writers likely results in a deadlock
 * scenario.
 *
 * @tparam ChannelT
 */

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

    ChannelProvider(std::unique_ptr<ChannelT> channel) : m_channel(std::move(channel)) {}
    ~ChannelProvider() = default;

    std::shared_ptr<ReadableChannel> readable_channel()
    {
        std::shared_ptr<ReadableChannel> readable = m_readable_channel.lock();
        if (readable == nullptr)
        {
            auto channel = m_channel;
            readable     = {new ReadableChannel(channel), [channel](ReadableChannel* readable) {
                            DVLOG(10) << "readable channel completed - issuing soft close on backing channel "
                                      << channel.get();
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
        std::shared_ptr<WritableChannel> writable = m_readable_channel.lock();
        if (writable == nullptr)
        {
            auto channel = m_channel;
            writable     = {new WritableChannel(channel), [channel](WritableChannel* writable) {
                            DVLOG(10) << "writable channel completed - issuing hard close on backing channel "
                                      << channel.get();
                            delete writable;
                            channel->close();  // todo(ryan) - implement kill on channel
                        }};

            m_writable_channel   = writable;
            m_writable_persisent = writable;
        }
        return writable;
    }

  private:
    std::shared_ptr<ChannelT> m_channel;

    std::weak_ptr<ReadableChannel> m_readable_channel;
    std::shared_ptr<ReadableChannel> m_readable_persisent;

    std::weak_ptr<WritableChannel> m_writable_channel;
    std::shared_ptr<WritableChannel> m_writable_persisent;
};

template <concepts::data_type ChannelT>
auto make_channel_provider(std::unique_ptr<ChannelT> channel)
{
    return ChannelProvider<ChannelT>(std::move(channel));
}

}  // namespace mrc::channel::v2
