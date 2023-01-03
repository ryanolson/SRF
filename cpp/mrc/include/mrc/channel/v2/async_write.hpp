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

#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/channel/v2/cpo/write.hpp"

namespace mrc::channel::v2 {

template <concepts::writable ChannelT>
[[nodiscard]] inline auto async_write(ChannelT& channel, typename ChannelT::data_type&& data) -> decltype(auto)
{
    if constexpr (concepts::concrete_writable<ChannelT>)
    {
        return cpo::async_write(channel, std::move(data));
    }
    else
    {
        return channel.write_task(std::move(data));
    }
}

}  // namespace mrc::channel::v2
