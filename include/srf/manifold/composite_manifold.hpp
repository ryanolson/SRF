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

#include "srf/codable/type_traits.hpp"
#include "srf/core/utils.hpp"
#include "srf/manifold/egress.hpp"
#include "srf/manifold/ingress.hpp"
#include "srf/manifold/manifold.hpp"
#include "srf/pubsub/publisher.hpp"
#include "srf/pubsub/state.hpp"
#include "srf/pubsub/subscriber.hpp"
#include "srf/segment/utils.hpp"

#include <memory>

namespace srf::manifold {

template <typename IngressT, typename EgressT>
class CompositeManifold : public Manifold
{
    static_assert(std::is_base_of_v<IngressDelegate, IngressT>, "ingress must be derived from IngressDelegate");
    static_assert(std::is_base_of_v<EgressDelegate, EgressT>, "ingress must be derived from EgressDelegate");

  public:
    CompositeManifold(PortName port_name, core::IRuntime& resources) : Manifold(std::move(port_name), resources)
    {
        // construct IngressT and EgressT on the NUMA node / memory domain in which the object will run
        this->resources()
            .main()
            .enqueue([this] {
                this->set_ingress(std::make_unique<IngressT>());
                this->set_egress(std::make_unique<EgressT>());
            })
            .get();
    }
    CompositeManifold(PortName port_name,
                      core::IRuntime& resources,
                      std::unique_ptr<IngressT> ingress,
                      std::unique_ptr<EgressT> egress) :
      Manifold(std::move(port_name), resources)
    {
        this->set_egress(std::move(egress));
        this->set_ingress(std::move(ingress));
    }

    ~CompositeManifold()
    {
        VLOG(10) << "Destroying CompositeManifold: " << this->info();

        // Release the changed connection notifications before destroying the
        this->set_publisher(nullptr);
        this->set_suscriber(nullptr);

        CHECK_EQ(m_input_updates.size(), 0) << "Manifold being destroyed with pending updates";
        CHECK_EQ(m_output_updates.size(), 0) << "Manifold being destroyed with pending updates";
    }

  protected:
    IngressT& ingress()
    {
        auto& casted = dynamic_cast<IngressT&>(this->get_ingress());
        return casted;
    }

    EgressT& egress()
    {
        auto& casted = dynamic_cast<EgressT&>(this->get_egress());
        return casted;
    }

  private:
    void do_add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source) final
    {
        // enqueue update to be done later
        m_input_updates.push_back([this, address, input_source] {
            DVLOG(10) << info() << ": ingress attaching to upstream segment " << segment::info(address);
            this->ingress().add_input(address, input_source);
            on_add_input(address);

            // This means we have a local connection, create a publisher
            if (!this->has_publisher() && this->can_have_remote_connections())
            {
                // Make the publisher
                using ingress_t = typename IngressT::data_t;

                if constexpr (codable::is_codable_v<ingress_t>)
                {
                    auto publisher = pubsub::make_publisher<pubsub::PublisherRoundRobin<ingress_t>>(this->port_name(),
                                                                                                    this->runtime());

                    // publisher->register_connections_changed_handler(
                    //     [this, pub_ptr = publisher.get()](const pubsub::PublisherBase::tagged_members_t& connections)
                    //     {
                    //         // Here we want to basically add/remove inputs as connections are made
                    //         for (const auto& conn : connections)
                    //         {
                    //             if (conn.second.state == pubsub::SubscriptionState::Connected)
                    //             {
                    //                 m_egress->add_output(conn.first, pub_ptr);
                    //             }
                    //             else
                    //             {
                    //                 m_egress->remove_output(conn.first);
                    //             }
                    //         }

                    //         // this->update_outputs();
                    //     });

                    // this->request_update();

                    this->set_publisher(publisher);
                }
                else
                {
                    LOG(WARNING) << "Cannot make a Pub/Sub connection since type `" << type_name<ingress_t>()
                                 << "` is not codeable";
                }
            }
        });
    }

    void do_add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink) final
    {
        // enqueue update to be done later
        m_output_updates.push_back([this, address, output_sink] {
            DVLOG(10) << info() << ": egress attaching to downstream segment " << segment::info(address);
            this->egress().add_output(address, output_sink);
            on_add_output(address);

            // This means we have a local connection, create a publisher
            if (!this->has_suscriber() && this->can_have_remote_connections())
            {
                // Make the publisher
                using egress_t = typename EgressT::data_t;

                if constexpr (codable::is_codable_v<egress_t>)
                {
                    auto subscriber =
                        pubsub::make_subscriber<pubsub::Subscriber<egress_t>>(this->port_name(), this->runtime());

                    // subscriber->register_connections_changed_handler(
                    //     [this, sub_ptr = subscriber.get()](const pubsub::PublisherBase::tagged_members_t&
                    //     connections) {
                    //         // Here we want to basically add/remove inputs as connections are made
                    //         for (const auto& conn : connections)
                    //         {
                    //             if (conn.second.state == pubsub::SubscriptionState::Connected)
                    //             {
                    //                 m_ingress->add_input(conn.first, sub_ptr);
                    //             }
                    //             else
                    //             {
                    //                 m_ingress->remove_input(conn.first);
                    //             }
                    //         }

                    //         // this->update_inputs();
                    //     });

                    // this->request_update();

                    this->set_suscriber(subscriber);
                }
                else
                {
                    LOG(WARNING) << "Cannot make a Pub/Sub connection since type `" << type_name<egress_t>()
                                 << "` is not codeable";
                }
            }
        });
    }

    void update(std::vector<std::function<void()>>& updates)
    {
        resources()
            .main()
            .enqueue([&] {
                for (auto& update_fn : updates)
                {
                    update_fn();
                }
            })
            .get();
        updates.clear();
    }

    void update_inputs() final
    {
        will_update_inputs();
        if (!m_input_updates.empty())
        {
            DVLOG(10) << info() << ": issuing all enqueued input updates";
            update(m_input_updates);
            DVLOG(10) << port_name() << " manifold finished input updates";
        }
    }

    void update_outputs() final
    {
        will_update_outputs();
        if (!m_output_updates.empty())
        {
            DVLOG(10) << info() << ": issuing all enqueued output updates";
            update(m_output_updates);
            DVLOG(10) << port_name() << " manifold finished output updates";
        }
    }

    virtual void on_add_input(const SegmentAddress& address) {}
    virtual void on_add_output(const SegmentAddress& address) {}

    virtual void will_update_inputs() {}
    virtual void will_update_outputs() {}

    std::vector<std::function<void()>> m_input_updates;
    std::vector<std::function<void()>> m_output_updates;

    // std::unique_ptr<IngressT> m_ingress;
    // std::unique_ptr<EgressT> m_egress;
};

}  // namespace srf::manifold
