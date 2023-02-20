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

#include <map>
#include <optional>

namespace mrc::ops {

/**
 * make_edge(source, sink)
 * - creates an edge object
 * - offers the edge object to the sink
 *   - sink returns its connectability: Any, Direct, Channel, None
 *   - edge stores sink's connectability state
 * - if not none, then the edge object is offered to source
 *   - source returns its connectability: Any, Direct, Channel, None
 *   - edge stores source's connectability state
 * - edge is still mutable
 * - egde sets default type in order:
 *   - Direct, Immediate/BufferedChannel
 * - edge may hold shared pointers to the source and sink
 */

enum class ConnectableStatus
{
    Any,
    Channel,
    Direct,
    None
};

class Vertex;

template <typename T>
class Connectors;

template <typename T>
class Connector;

struct ConnectorType
{};

struct Reader : ConnectorType
{};
struct Writer : ConnectorType
{};

class Vertex
{
  public:
    Vertex(Connectors<Writer>& sources, Connectors<Reader>& sinks) : m_sources(sources), m_sinks(sinks) {}

  private:
    Connectors<Writer>& m_sources;
    Connectors<Reader>& m_sinks;
};

template <typename T>
class Connectors
{
  public:
  private:
    std::map<std::size_t, Connector<T>*> m_edges;
};

template <typename T>
class Connector
{
    virtual ConnectableStatus connectable_status() const = 0;
};

}  // namespace mrc::ops
