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
#include "mrc/channel/v2/async_read.hpp"
#include "mrc/channel/v2/concepts/readable.hpp"
#include "mrc/channel/v2/connectors/channel_acceptor.hpp"

namespace mrc::ops {

namespace detail {

template <channel::v2::concepts::readable ChannelT>
class InputImpl : public channel::v2::ChannelAcceptor<ChannelT>
{
  protected:
    inline auto async_read() noexcept -> decltype(auto)
    {
        return channel::v2::async_read(this->channel());
    }
};

}  // namespace detail

template <typename T>
struct Input;

// template specialization for concrete channel types
template <channel::v2::concepts::readable ChannelT>
struct Input<ChannelT> : public detail::InputImpl<ChannelT>
{};

// template specialization for data types
template <std::movable DataT>
struct Input<DataT> : public detail::InputImpl<channel::v2::IReadableChannel<DataT>>
{};

template <typename... Types>  // NOLINT
struct Inputs : private std::tuple<Input<Types>...>
{
    using output_type = Inputs<Types...>;

    template <std::size_t Id>
    auto& get_input()
    {
        return std::get<Id>(*this);
    }
};

}  // namespace mrc::ops
