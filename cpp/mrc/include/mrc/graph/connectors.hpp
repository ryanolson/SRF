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

#include <cstdint>
#include <memory>

namespace mrc::graph {

template <typename T>
class Connector;

template <typename T>
class Connectors
{
  public:
    using connector_type = T;

  protected:
    virtual void add_connector(std::size_t tag, Connector<T>& connector) = 0;
    virtual Connector<T>& get_connector(std::size_t tag)                 = 0;
};

template <typename T>
class Connector
{
  public:
    using connector_type = T;

    virtual ConnectableStatus connectable_status() const = 0;
};

}  // namespace mrc::graph
