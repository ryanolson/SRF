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
#include "mrc/graph/connectors.hpp"

#include <glog/logging.h>

#include <cstdint>
#include <memory>

namespace mrc::graph {

class Connection
{
  public:
    Connection(Connector<Writer>& writer, Connector<Reader>& reader) : m_writer(writer), m_reader(reader)
    {
        // evaluate connectable_status on both the reader and writer
        auto writer_conn_status = m_writer.connectable_status();
        auto reader_conn_status = m_writer.connectable_status();

        CHECK(writer_conn_status != ConnectableStatus::Connected);
        CHECK(reader_conn_status != ConnectableStatus::Connected);

        // start with writer
        unsigned status = static_cast<unsigned>(writer_conn_status) & static_cast<unsigned>(reader_conn_status);
        m_status = static_cast<ConnectableStatus>(status);
    }

  private:
    Connector<Writer>& m_writer;
    Connector<Reader>& m_reader;
    ConnectableStatus m_status;
};

}  // namespace mrc::graph
