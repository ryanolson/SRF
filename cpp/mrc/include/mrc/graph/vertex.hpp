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

#include "mrc/graph/connection_type.hpp"

#include <map>
#include <optional>

namespace mrc::graph {

template <typename T>
class Connectors;

class Vertex
{
  public:
    Vertex(Connectors<Writer>& sources, Connectors<Reader>& sinks) : m_sources(sources), m_sinks(sinks) {}

  private:
    Connectors<Writer>& m_sources;
    Connectors<Reader>& m_sinks;
};

}  // namespace mrc::graph
