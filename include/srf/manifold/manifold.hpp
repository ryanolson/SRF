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

#include "srf/core/runtime.hpp"
#include "srf/manifold/interface.hpp"
#include "srf/node/sink_properties.hpp"
#include "srf/node/source_properties.hpp"
#include "srf/pipeline/resources.hpp"
#include "srf/pubsub/publisher.hpp"
#include "srf/pubsub/subscriber.hpp"
#include "srf/types.hpp"

#include <memory>
#include <string>

namespace srf::manifold {

class Manifold : public Interface
{
  public:
    Manifold(PortName port_name, core::IRuntime& resources);

    const PortName& port_name() const final;

  protected:
    core::IRuntime& runtime() const;

    pipeline::Resources& resources() const;

    const std::string& info() const;

    bool can_have_remote_connections() const;

    bool has_publisher() const;
    bool has_suscriber() const;

    void set_publisher(std::shared_ptr<pubsub::PublisherBase> pub);
    void set_suscriber(std::shared_ptr<pubsub::SubscriberBase> sub);

    void request_update() const;

  private:
    void add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source) final;

    void add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink) final;

    virtual void do_add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source) = 0;
    virtual void do_add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink)   = 0;

    PortName m_port_name;
    core::IRuntime& m_runtime;
    std::string m_info;

    // Pub/Sub pieces
    std::shared_ptr<pubsub::PublisherBase> m_publisher;
    std::shared_ptr<pubsub::SubscriberBase> m_subscriber;
};

}  // namespace srf::manifold
