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

#include "srf/manifold/manifold.hpp"

#include "internal/pubsub/publisher_manager.hpp"
#include "internal/runnable/resources.hpp"
#include "internal/runtime/runtime.hpp"

#include "srf/node/sink_properties.hpp"
#include "srf/node/source_properties.hpp"
#include "srf/pipeline/resources.hpp"
#include "srf/segment/utils.hpp"
#include "srf/types.hpp"

#include <glog/logging.h>

#include <ostream>
#include <string>
#include <utility>

namespace srf::manifold {

Manifold::Manifold(PortName port_name, core::IRuntime& resources) :
  m_port_name(std::move(port_name)),
  m_runtime(resources)
{}

const PortName& Manifold::port_name() const
{
    return m_port_name;
}

core::IRuntime& Manifold::runtime() const
{
    return m_runtime;
}

pipeline::Resources& Manifold::resources() const
{
    return m_runtime.runnable();
}

const std::string& Manifold::info() const
{
    return m_info;
}

bool Manifold::can_have_remote_connections() const
{
    auto& internal_runtime = dynamic_cast<internal::runtime::Runtime&>(this->m_runtime);

    // Check the options and see if its possible to get incoming remote connections (must have architect URL)
    return !internal_runtime.resources().system().options().architect_url().empty();
}

void Manifold::add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source)
{
    DVLOG(3) << "manifold " << this->port_name() << ": connecting to upstream segment " << segment::info(address);
    do_add_input(address, input_source);
    DVLOG(10) << "manifold " << this->port_name() << ": completed connection to upstream segment "
              << segment::info(address);
}

void Manifold::add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink)
{
    DVLOG(3) << "manifold " << this->port_name() << ": connecting to downstream segment " << segment::info(address);
    do_add_output(address, output_sink);
    DVLOG(10) << "manifold " << this->port_name() << ": completed connection to downstream segment "
              << segment::info(address);
}

}  // namespace srf::manifold
