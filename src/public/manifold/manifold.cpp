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

Manifold::~Manifold()
{
    if (m_publisher)
    {
        m_publisher->await_join();
    }

    if (m_subscriber)
    {
        m_subscriber->await_join();
    }
}

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

EgressDelegate& Manifold::get_egress() const
{
    return *m_egress;
}

IngressDelegate& Manifold::get_ingress() const
{
    return *m_ingress;
}

void Manifold::set_egress(std::unique_ptr<EgressDelegate>&& egress)
{
    m_egress = std::move(egress);
}

void Manifold::set_ingress(std::unique_ptr<IngressDelegate>&& ingress)
{
    m_ingress = std::move(ingress);
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

bool Manifold::has_publisher() const
{
    return m_publisher != nullptr;
}

bool Manifold::has_suscriber() const
{
    return m_subscriber != nullptr;
}

void Manifold::set_publisher(std::shared_ptr<pubsub::PublisherBase> pub)
{
    if (m_publisher)
    {
        // Disconnect any change notifications from the previous one
        m_pub_changed_handle.release();
    }

    m_publisher = pub;

    // Now subscribe to changes
    if (m_publisher)
    {
        auto* pub_sink = dynamic_cast<node::SinkPropertiesBase*>(m_publisher.get());

        m_pub_changed_handle = m_publisher->register_connections_changed_handler(
            [this, pub_sink](const pubsub::PublisherBase::tagged_members_t& connections) {
                // Here we want to basically add/remove inputs as connections are made
                for (const auto& conn : connections)
                {
                    if (conn.second.state == pubsub::SubscriptionState::Connected)
                    {
                        m_egress->add_output(conn.first, pub_sink);
                    }
                    else
                    {
                        m_egress->remove_output(conn.first);
                    }
                }
            });

        this->request_update();
    }
}

void Manifold::set_suscriber(std::shared_ptr<pubsub::SubscriberBase> sub)
{
    if (m_subscriber)
    {
        // Disconnect any change notifications from the previous one
        m_sub_changed_handle.release();
    }

    m_subscriber = sub;

    // Now subscribe to changes
    if (m_subscriber)
    {
        auto* sub_source = dynamic_cast<node::SourcePropertiesBase*>(m_subscriber.get());

        m_sub_changed_handle = m_subscriber->register_connections_changed_handler(
            [this, sub_source](const pubsub::PublisherBase::tagged_members_t& connections) {
                // Here we want to basically add/remove inputs as connections are made
                for (const auto& conn : connections)
                {
                    if (conn.second.state == pubsub::SubscriptionState::Connected)
                    {
                        m_ingress->add_input(conn.first, sub_source);
                    }
                    else
                    {
                        m_ingress->remove_input(conn.first);
                    }
                }
            });

        this->request_update();
    }
}

void Manifold::request_update() const
{
    auto& internal_runtime = dynamic_cast<internal::runtime::Runtime&>(this->m_runtime);

    internal_runtime.resources().network()->control_plane().client().request_update();
}
}  // namespace srf::manifold
