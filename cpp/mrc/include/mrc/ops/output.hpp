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
#include "mrc/channel/v2/writable_channel.hpp"
#include "mrc/core/error.hpp"
#include "mrc/utils/macros.hpp"

namespace mrc::ops {

template <typename T>
using WritableChannelHandle = std::shared_ptr<channel::v2::IWritableChannel<T>>;  // NOLINT

template <typename T>
class Output
{
  public:
    Output() = default;
    Output(WritableChannelHandle<T> writable_channel) : m_writable_channel(std::move(writable_channel)) {}

    DELETE_COPYABILITY(Output);
    DELETE_MOVEABILITY(Output);

    [[nodiscard]] auto async_write(T&& data) -> decltype(auto)
    {
        return m_writable_channel->async_write(std::move(data));
    }

    bool is_connected() const
    {
        std::lock_guard lock(m_mutex);
        return bool(m_writable_channel);
    }

    void connect_channel(WritableChannelHandle<T> writable_channel)
    {
        std::lock_guard lock(m_mutex);
        if (m_writable_channel)
        {
            throw Error::create(
                "input already has a channel connected; disconnect existing connection before trying to make a new "
                "connection");
        }
        m_writable_channel = writable_channel;
    }

    void disconnect_channel()
    {
        std::lock_guard lock(m_mutex);
        m_writable_channel.reset();
    }

    WritableChannelHandle<T> transfer_channel()
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_writable_channel, nullptr);
    }

  private:
    std::shared_ptr<channel::v2::IWritableChannel<T>> m_writable_channel;
    mutable std::mutex m_mutex;
};

template <typename T>
struct SingleOutput : public Output<T>
{
    using output_type = SingleOutput<T>;
};

template <typename... Types>  // NOLINT
struct MultipleOutputs : private std::tuple<SingleOutput<Types>...>
{
    using output_type = MultipleOutputs<Types...>;

    template <std::size_t Id>
    auto& get_output()
    {
        return std::get<Id>(*this);
    }
};

}  // namespace mrc::ops
