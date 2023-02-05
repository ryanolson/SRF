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

#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/scheduling_terms/on_next_data.hpp"

#include <optional>

namespace mrc::ops {

// struct VertexInfo
// {};

// class EdgeBase
// {
//   private:
//     std::shared_ptr<VertexInfo> m_source{nullptr};
//     std::shared_ptr<VertexInfo> m_sink{nullptr};
// };

// template <typename EdgeT>
// class Edge;

// direct edges are single use
// the reader will yield until the writer has set OnNextData scheduling term
template <typename DataT>
class DirectEdge
{
  public:
    using data_type = DataT;
    //

    // cpo: set_writer(edge, scheduling_term_of<DataT> auto&& scheduling_term)
    // cpo: co_await get_reader(edge) -> awaitable_of<scheduling_term_of<DataT>>

  private:
    std::optional<OnNextData<DataT>> m_scheduling_term;
};

template <channel::v2::concepts::channel ChannelT>
class ChannelEdge
{
  public:
    using data_type = typename ChannelT::data_type;

  private:
    std::shared_ptr<ChannelT> m_channel;
};

}  // namespace mrc::ops
