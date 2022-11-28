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

#include "rxcpp/operators/rx-map.hpp"

#include "srf/codable/encoded_object.hpp"
#include "srf/core/runtime.hpp"
#include "srf/node/edge_builder.hpp"
#include "srf/node/generic_sink.hpp"
#include "srf/node/rx_node.hpp"
#include "srf/node/rx_sink.hpp"
#include "srf/node/sink_channel.hpp"
#include "srf/node/sink_properties.hpp"
#include "srf/node/source_channel.hpp"
#include "srf/pubsub/state.hpp"
#include "srf/remote_descriptor/storage.hpp"
#include "srf/runnable/forward.hpp"
#include "srf/runnable/launch_control.hpp"
#include "srf/runnable/launch_options.hpp"
#include "srf/runnable/runnable.hpp"
#include "srf/runnable/runner.hpp"
#include "srf/types.hpp"
#include "srf/utils/macros.hpp"

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace srf::internal::pubsub {
class PubSubBase;
}

namespace srf::pubsub {

class ClientSubscriptionBase;

// When this goes out of scope, it will disable the release handle
class ClientSubscriptionBaseChangeHandle
{
  public:
    ClientSubscriptionBaseChangeHandle()                                                = default;
    ClientSubscriptionBaseChangeHandle(const ClientSubscriptionBaseChangeHandle& other) = delete;
    ClientSubscriptionBaseChangeHandle(ClientSubscriptionBaseChangeHandle&& other);
    ClientSubscriptionBaseChangeHandle(ClientSubscriptionBase* parent, size_t handle_id);

    ~ClientSubscriptionBaseChangeHandle();

    ClientSubscriptionBaseChangeHandle& operator=(const ClientSubscriptionBaseChangeHandle& other) = delete;
    ClientSubscriptionBaseChangeHandle& operator=(ClientSubscriptionBaseChangeHandle&& other);

    void release();

    size_t m_handle_id{0};
    ClientSubscriptionBase* m_parent{nullptr};
};

class ClientSubscriptionBase
{
  public:
    using tagged_members_t              = std::unordered_map<TagID, SubscriptionMember>;
    using connections_changed_handler_t = std::function<void(const tagged_members_t&)>;

    virtual ~ClientSubscriptionBase();

    const std::string& service_name() const;
    const std::uint64_t& tag() const;
    virtual const std::string& role() const = 0;

    ClientSubscriptionBaseChangeHandle register_connections_changed_handler(
        connections_changed_handler_t on_changed_fn);

    virtual void close();

    void await_join();

    size_t await_connections();

    void await_completed();

  protected:
    ClientSubscriptionBase(std::string service_name, core::IRuntime& runtime);

    void set_linked_service(std::uint64_t tag, std::function<void()> drop_service_fn);

    const std::unordered_map<std::uint64_t, InstanceID>& get_tagged_instances() const;

    std::unique_ptr<codable::EncodedObject> get_encoded_obj() const;

    virtual void on_tagged_instances_updated();

  private:
    // std::unique_ptr<runnable::Runner> link_service(
    //     std::uint64_t tag,
    //     std::function<void()> drop_service_fn,
    //     runnable::LaunchControl& launch_control,
    //     runnable::LaunchOptions& launch_options,
    //     node::SinkProperties<std::pair<std::uint64_t, std::unique_ptr<srf::remote_descriptor::Storage>>>& data_sink);

    // virtual std::unique_ptr<runnable::Runner> do_link_service(
    //     std::uint64_t tag,
    //     std::function<void()> drop_service_fn,
    //     runnable::LaunchControl& launch_control,
    //     runnable::LaunchOptions& launch_options,
    //     node::SinkProperties<std::pair<std::uint64_t, std::unique_ptr<srf::remote_descriptor::Storage>>>&
    //         data_sink) = 0;

    void set_linked_service_completed();

    void update_tagged_members(SubscriptionState state, const tagged_members_t& tagged_members);

    void release_connection_changed_handler(size_t handle_id);

    core::IRuntime& m_runtime;
    const std::string m_service_name;
    TagID m_tag;
    tagged_members_t m_tagged_members;
    std::unordered_map<TagID, InstanceID> m_active_tagged_instances;

    std::atomic<size_t> m_handle_increment{0};
    std::once_flag m_drop_service_fn_called;
    std::function<void()> m_drop_service_fn;
    std::map<size_t, connections_changed_handler_t> m_on_connections_changed_fns;

    SharedFuture<void> m_join_future;
    Promise<void> m_service_completed_promise;

    SubscriptionState m_state{SubscriptionState::Watcher};
    boost::fibers::mutex m_mutex;
    boost::fibers::condition_variable m_tagged_cv;

    friend srf::internal::pubsub::PubSubBase;
    friend ClientSubscriptionBaseChangeHandle;
};

}  // namespace srf::pubsub
