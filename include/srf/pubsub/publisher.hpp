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

#include "srf/codable/encoded_object.hpp"
#include "srf/core/runtime.hpp"
#include "srf/node/edge_builder.hpp"
#include "srf/node/generic_sink.hpp"
#include "srf/node/rx_sink.hpp"
#include "srf/node/sink_channel.hpp"
#include "srf/node/source_channel.hpp"
#include "srf/remote_descriptor/storage.hpp"
#include "srf/runnable/forward.hpp"
#include "srf/runnable/launch_control.hpp"
#include "srf/runnable/launch_options.hpp"
#include "srf/runnable/runnable.hpp"
#include "srf/runnable/runner.hpp"
#include "srf/types.hpp"
#include "srf/utils/macros.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace srf::pubsub {

// class PublisherManager;

template <typename T>
class Publisher;

class PublisherBase : public runnable::Runnable
{
    using state_t = runnable::Runnable::State;

  public:
    using connections_changed_handler_t = std::function<void(const std::unordered_map<std::uint64_t, InstanceID>&)>;

    const std::string& service_name() const;
    const std::uint64_t& tag() const;

    // void set_tagged_instances(const std::string& role,
    //                           const std::unordered_map<std::uint64_t, InstanceID>& tagged_instances)
    // {
    //     // todo - convert tagged instances -> tagged endpoints
    //     m_tagged_instances = tagged_instances;
    //     for (const auto& [tag, instance_id] : m_tagged_instances)
    //     {
    //         m_tagged_endpoints[tag] = resources().network()->data_plane().client().endpoint_shared(instance_id);
    //     }
    //     m_publisher->on_update();
    // }

    std::unique_ptr<runnable::Runner> link_service(std::uint64_t tag,
                                                   std::function<void()> drop_service_fn,
                                                   runnable::LaunchControl& launch_control,
                                                   runnable::LaunchOptions& launch_options);

    void update_tagged_instances(const std::unordered_map<std::uint64_t, InstanceID>& tagged_instances);

    void register_connections_changed_handler(connections_changed_handler_t on_changed_fn);

  protected:
    PublisherBase(std::string service_name, core::IRuntime& runtime);

    const std::unordered_map<std::uint64_t, InstanceID>& get_tagged_instances() const;

    std::unique_ptr<codable::EncodedObject> get_encoded_obj() const;

    void push_object(std::uint64_t id, std::unique_ptr<remote_descriptor::Storage> storage);

    virtual void on_tagged_instances_updated();

  private:
    void main(runnable::Context& context) final;
    void on_state_update(const state_t& state) final;

    virtual std::unique_ptr<runnable::Runner> do_link_service(std::uint64_t tag,
                                                              std::function<void()> drop_service_fn,
                                                              runnable::LaunchControl& launch_control,
                                                              runnable::LaunchOptions& launch_options) = 0;

    core::IRuntime& m_runtime;
    std::atomic<bool> m_running{false};
    const std::string m_service_name;
    std::uint64_t m_tag;
    std::unordered_map<std::uint64_t, InstanceID> m_tagged_instances;

    std::vector<connections_changed_handler_t> m_on_connections_changed_fns;

    // friend PublisherManager;
};

class PublisherEdgeBase
{
  public:
    DELETE_COPYABILITY(PublisherEdgeBase);
    DELETE_MOVEABILITY(PublisherEdgeBase);

    const std::string& service_name();

    const std::uint64_t& tag();

    void register_connections_changed_handler(PublisherBase::connections_changed_handler_t on_changed_fn);

  protected:
    PublisherEdgeBase(PublisherBase& parent);

  private:
    PublisherBase& m_parent;
};

template <typename T>
class PublisherEdge : public node::SinkChannel<T>, public node::SourceChannelWriteable<T>, public PublisherEdgeBase
{
    PublisherEdge(Publisher<T>& parent) : PublisherEdgeBase(parent) {}

  public:
    ~PublisherEdge() = default;

    // static std::shared_ptr<PublisherEdge<T>> make_pub_service(std::unique_ptr<Publisher<T>> pub,
    //                                                           core::IRuntime& runtime)
    // {
    //     // Get a future to the edge object
    //     auto edge_fut = pub->get_edge();

    //     // Call the function to build the publisher service
    //     auto drop_sub_fn = make_pub_service(std::move(pub), runtime);
    // }

  private:
    // Publisher<T>& m_parent;

    // friend void make_pub_service(std::shared_ptr<PublisherBase> publisher, core::IRuntime& runtime);

    // std::function<void()> m_drop_service_fn{};

    // Only allow publisher to make this class
    friend Publisher<T>;

    // // Allow this function to finish setting up m_drop_service_fn
    // template <typename PublisherT, typename... ArgsT>
    // friend auto make_publisher(std::string name, core::IRuntime& runtime, ArgsT&&... args);
};

template <typename T>
class Publisher : public PublisherBase
{
    Publisher(std::string service_name, core::IRuntime& runtime) : PublisherBase(std::move(service_name), runtime) {}

  public:
    using data_t = T;

    ~Publisher() override = default;

    // DELETE_COPYABILITY(Publisher);
    // DELETE_MOVEABILITY(Publisher);

    std::unique_ptr<runnable::Runner> do_link_service(std::uint64_t tag,
                                                      std::function<void()> drop_service_fn,
                                                      runnable::LaunchControl& launch_control,
                                                      runnable::LaunchOptions& launch_options) override
    {
        // Now that we have the tag and drop service function, make the edge object
        auto edge =
            std::shared_ptr<PublisherEdge<T>>(new PublisherEdge<T>(*this), [drop_service_fn](PublisherEdge<T>* ptr) {
                // Call the function to stop the service
                drop_service_fn();
                delete ptr;
            });

        // Create the sink runnable that will serve as the progress engine
        auto sink = std::make_unique<srf::node::RxSink<T>>([this](T data) {
            // Forward to our class
            this->on_data(std::move(data));
        });

        // Link the edge and the sink
        srf::node::make_edge(*edge, *sink);

        auto writer = launch_control.prepare_launcher(launch_options, std::move(sink))->ignition();

        // Set the new edge to the edge future
        m_edge_promise.set_value(std::move(edge));

        return writer;
    }

  private:
    Future<std::shared_ptr<PublisherEdge<T>>> get_edge()
    {
        // CHECK(m_edge_promise) << "Edge was already used or service was not properly connectd!";

        return m_edge_promise.get_future();
    }

    void on_data(T&& object)
    {
        // Block until we have tagged instances
        while (this->get_tagged_instances().empty())
        {
            // await subscribers
            // for now just return and drop the object
            boost::this_fiber::yield();
        }

        this->write(std::move(object));
    }

    virtual void write(T&& object) = 0;

    Promise<std::shared_ptr<PublisherEdge<T>>> m_edge_promise;

    // friend PublisherManager;

    template <typename PublisherT, typename... ArgsT>
    friend auto make_publisher(std::string name, core::IRuntime& runtime, ArgsT&&... args);
};

template <typename T>
class PublisherRoundRobin : public Publisher<T>
{
  public:
    using Publisher<T>::Publisher;

  private:
    void on_tagged_instances_updated() final
    {
        // Save the IDs in a list for quick lookup
        m_tagged_ids.clear();

        for (auto const& id : this->get_tagged_instances())
        {
            m_tagged_ids.push_back(id.first);
        }
        m_next_idx = 0;

        // Make sure to call the base
        Publisher<T>::on_tagged_instances_updated();
    }

    void write(T&& object) final
    {
        auto storage = remote_descriptor::TypedStorage<T>::create(std::move(object), this->get_encoded_obj());

        auto next_id = m_tagged_ids[m_next_idx++ % m_tagged_ids.size()];

        this->push_object(next_id, std::move(storage));
    }

    std::atomic<std::size_t> m_next_idx{0};
    std::vector<std::uint64_t> m_tagged_ids;
};

template <typename PublisherT, typename... ArgsT>
auto make_publisher(std::string name, core::IRuntime& runtime, ArgsT&&... args)
{
    // Assert that PublisherT derives from publisher

    // Get the data type
    using data_t = typename PublisherT::data_t;

    // // Create the edge object
    // auto pub_edge = std::make_shared<PublisherEdge<data_t>>();

    // Create the actual publisher
    auto pub = std::unique_ptr<PublisherT>(new PublisherT(std::move(name), runtime, std::forward<ArgsT>(args)...));

    // Get a future to the edge that will be created during make_pub_service
    auto pub_edge_future = pub->get_edge();

    // Now build the service
    make_pub_service(std::move(pub), runtime);

    // // Now set the drop function into the publisher edge
    // pub_edge->m_drop_service_fn = std::move(drop_service_fn);

    // Finally, return the edge
    return pub_edge_future.get();
}

void make_pub_service(std::unique_ptr<PublisherBase> publisher, core::IRuntime& runtime);

}  // namespace srf::pubsub
