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

#include "internal/network/resources.hpp"

#include "internal/control_plane/client.hpp"
#include "internal/control_plane/resources.hpp"
#include "internal/data_plane/resources.hpp"
#include "internal/data_plane/server.hpp"
#include "internal/memory/host_resources.hpp"
#include "internal/remote_descriptor/remote_descriptor.hpp"
#include "internal/resources/forward.hpp"
#include "internal/resources/partition_resources_base.hpp"
#include "internal/ucx/registration_cache.hpp"
#include "internal/ucx/resources.hpp"

namespace srf::internal::network {

Resources::Resources(resources::PartitionResourceBase& base,
                     ucx::Resources& ucx,
                     memory::HostResources& host,
                     std::unique_ptr<control_plane::client::Instance> control_plane) :
  resources::PartitionResourceBase(base),
  m_control_plane(std::move(control_plane)),
  m_remote_descriptors(std::make_shared<remote_descriptor::Manager>(m_control_plane->instance_id()))
{
    CHECK(m_control_plane);
    CHECK_LT(partition_id(), m_control_plane->client().connections().instance_ids().size());
    CHECK_EQ(m_control_plane->instance_id(), m_control_plane->client().connections().instance_ids().at(partition_id()));

    // construct resources on the srf_network task queue thread
    ucx.network_task_queue()
        .enqueue([this, &base, &ucx, &host] {
            // initialize data plane services - server / client
            m_data_plane = std::make_unique<data_plane::Resources>(base, ucx, host, *m_control_plane);
            // m_control_plane->attach_data_plane_client(&m_data_plane->client());
        })
        .get();
}

Resources::~Resources()
{
    // this will sync with the control plane server to drop the instance
    // when this completes, we can disable the data plane
    m_control_plane.reset();

    if (m_data_plane)
    {
        m_data_plane->service_stop();
        m_data_plane->service_await_join();
    }
}

data_plane::Resources& Resources::data_plane()
{
    CHECK(m_data_plane);
    return *m_data_plane;
}

control_plane::client::Instance& Resources::control_plane()
{
    CHECK(m_control_plane);
    return *m_control_plane;
}

const InstanceID& Resources::instance_id() const
{
    return m_instance_id;
}

remote_descriptor::Manager& Resources::remote_descriptor_manager()
{
    CHECK(m_remote_descriptors);
    return *m_remote_descriptors;
}
}  // namespace srf::internal::network
