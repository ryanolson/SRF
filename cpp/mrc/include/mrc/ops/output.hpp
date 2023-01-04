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
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/channel/v2/connectors/channel_acceptor.hpp"
#include "mrc/core/error.hpp"
#include "mrc/utils/macros.hpp"

#include <type_traits>

namespace mrc::ops {

namespace detail {

template <channel::v2::concepts::writable ChannelT>
class OutputImpl : public channel::v2::ChannelAcceptor<ChannelT>
{
  protected:
    auto async_write(typename ChannelT::data_type&& data) noexcept -> decltype(auto)
    {
        return channel::v2::async_write(this->channel(), std::move(data));
    }
};

}  // namespace detail

template <typename T>
struct Output;

// template specialization for concrete channel types
template <channel::v2::concepts::writable ChannelT>
struct Output<ChannelT> : public detail::OutputImpl<ChannelT>
{};

// template specialization for data types
template <std::movable DataT>
struct Output<DataT> : public detail::OutputImpl<channel::v2::IWritableChannel<DataT>>
{};

template <typename... Types>  // NOLINT
struct Outputs : private std::tuple<Output<Types>...>
{
    using output_type = Outputs<Types...>;

    template <std::size_t Id>
    auto& get_output()
    {
        return std::get<Id>(*this);
    }
};

}  // namespace mrc::ops
