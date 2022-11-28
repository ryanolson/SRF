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

#include "internal/control_plane/client/state_manager.hpp"
#include "internal/control_plane/server/subscription_manager.hpp"
#include "internal/service.hpp"

#include "srf/channel/status.hpp"
#include "srf/node/edge_builder.hpp"
#include "srf/node/operators/router.hpp"
#include "srf/node/sink_channel.hpp"
#include "srf/node/source_channel.hpp"
#include "srf/protos/architect.pb.h"
#include "srf/types.hpp"
#include "srf/utils/macros.hpp"

#include <set>
#include <string>
#include <utility>

namespace srf::internal::control_plane::client {

class Instance;
class SubscriptionService;
class Role;

class SubscriptionService : public Service
{
  public:
    SubscriptionService(const std::string& service_name, Instance& instance);

    ~SubscriptionService() override;

    const std::string& service_name() const;

    virtual const std::string& role() const                         = 0;
    virtual const std::set<std::string>& roles() const              = 0;
    virtual const std::set<std::string>& subscribe_to_roles() const = 0;

    // can only be accessed after start
    Role& subscriptions(const std::string& role);
    const std::uint64_t& tag() const;

  protected:
    void do_service_start() override;
    std::function<void()> drop_subscription_service() const;
    Expected<> activate_subscription_service();

    virtual void update_tagged_members(
        ::srf::pubsub::SubscriptionState state,
        const std::unordered_map<std::uint64_t, ::srf::pubsub::SubscriptionMember>& members) = 0;

  private:
    Expected<> get_or_create_subscription_service();
    Expected<> register_subscription_service();

    const std::string m_service_name;
    std::uint64_t m_tag{0};
    Instance& m_instance;
    std::map<std::string, std::unique_ptr<Role>> m_subscriptions;

    friend Role;
    friend Instance;
};

class Role final
{
  public:
    Role(SubscriptionService& subscription_service, std::string role_name);
    virtual ~Role() = default;

    DELETE_COPYABILITY(Role);
    DELETE_MOVEABILITY(Role);

  private:
    void update_tagged_members(const std::unordered_map<std::uint64_t, InstanceID>& tagged_instances)
    {
        // m_subscription_service.update_tagged_members(m_role_name, tagged_instances);
        // std::lock_guard<decltype(m_mutex)> lock(m_mutex);
        // for (auto& p : m_update_promises)
        // {
        //     p.set_value();
        // }
        // m_update_promises.clear();
    }

    SubscriptionService& m_subscription_service;
    const std::string m_role_name;
    std::vector<Promise<void>> m_update_promises;
    std::mutex m_mutex;

    friend Instance;
};

}  // namespace srf::internal::control_plane::client
