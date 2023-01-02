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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/channel/v2/cpo/close.hpp"
#include "mrc/channel/v2/cpo/read.hpp"
#include "mrc/channel/v2/cpo/write.hpp"
#include "mrc/coroutines/task.hpp"

namespace mrc::channel::v2 {

template <std::movable T>
struct ChannelBase
{
    using data_type = T;

    virtual ~ChannelBase() = default;
};

template <concepts::channel ChannelT>
class Channel final : public ChannelT, public IChannel<typename ChannelT::data_type>
{
  public:
    using data_type = typename ChannelT::data_type;

    using ChannelT::ChannelT;
    ~Channel() final = default;

    [[nodiscard]] inline auto async_read() noexcept -> decltype(auto)
    {
        return cpo::async_read(*this);
    }

    [[nodiscard]] inline auto read_task() noexcept -> Task<expected<data_type, Status>> final
    {
        return cpo::read_task(*this);
    }

    [[nodiscard]] inline auto async_write(data_type&& data) noexcept -> decltype(auto)
    {
        return cpo::async_write(*this, std::move(data));
    }

    [[nodiscard]] inline auto write_task(data_type&& data) noexcept -> Task<> final
    {
        return cpo::write_task(*this, std::move(data));
    }

    // template <std::copy_constructible CopyT>
    // requires std::same_as<CopyT, data_type>
    // [[nodiscard]] inline auto async_write(CopyT data) noexcept -> decltype(auto)
    // {
    //     return async_write(std::move(data));
    // }

    auto close() noexcept -> void final
    {
        return cpo::close(*this);
    }
};

}  // namespace mrc::channel::v2
