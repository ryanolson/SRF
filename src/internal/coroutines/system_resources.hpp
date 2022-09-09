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

#include "coro/io_scheduler.hpp"
#include "coro/thread_pool.hpp"

#include "internal/coroutines/task_queue.hpp"
#include "internal/system/engine_factory_cpu_sets.hpp"
#include "internal/system/fiber_task_queue.hpp"
#include "internal/system/host_partition_provider.hpp"
#include "internal/system/resources.hpp"
#include "internal/system/system.hpp"
#include "internal/system/system_provider.hpp"
#include "public/utils/thread_utils.hpp"

#include "srf/core/bitmap.hpp"
#include "srf/core/task_queue.hpp"
#include "srf/pipeline/resources.hpp"
#include "srf/runnable/launch_control.hpp"

#include <cstddef>
#include <memory>
#include <thread>

namespace srf::internal::coroutines {

class SystemResources final : public system::HostPartitionProvider
{
  public:
    SystemResources(std::shared_ptr<system::System> system, std::size_t host_partition_id) :
      system::HostPartitionProvider(system::SystemProvider(system), host_partition_id)
    {
        for (const auto& [name, cpu_set] : host_partition().engine_factory_cpu_sets().fiber_cpu_sets)
        {
            DVLOG(10) << "constructing task queue for " << name << " on host_partition: " << host_partition_id
                      << " using cpus: " << cpu_set.str();
            m_task_queues[name] = std::make_unique<TaskQueue<coro::thread_pool>>(make_thread_pool(name, cpu_set));
        }
    }

  private:
    std::shared_ptr<coro::thread_pool> make_thread_pool(const std::string& desc, const Bitmap& cpu_set)
    {
        return std::make_shared<coro::thread_pool>(coro::thread_pool::options{
            .thread_count = static_cast<uint32_t>(cpu_set.weight()),
            .on_thread_start_functor =
                [this, cpu_set, desc](std::size_t thread_idx) { initialize_thread(desc, cpu_set, thread_idx); },
            .on_thread_stop_functor =
                [this, cpu_set, desc](std::size_t thread_idx) { deinitialize_thread(desc, cpu_set, thread_idx); },
        });
    }

    void initialize_thread(const std::string& desc, const Bitmap& cpu_set, const std::size_t& thread_idx)
    {
        const auto& options  = system().options();
        const auto& topology = system().topology();

        auto cpu_id        = cpu_set.vec().at(thread_idx);
        auto cpu_affinity  = CpuSet(cpu_id);
        auto numa_affinity = topology.numaset_for_cpuset(cpu_set);

        auto info = SRF_CONCAT_STR("[thread_pool: " << desc << "; thread_idx:" << thread_idx
                                                    << "; id: " << std::this_thread::get_id() << "]");
        DVLOG(10) << info << " initializing";
        set_current_thread_name(info);

        // todo(ryan) - enable thread/memory binding should be a system option, not specifically a fiber_pool option
        if (options.fiber_pool().enable_thread_binding())
        {
            DVLOG(10) << info << " hwloc_set_cpubind to cpu_id: " << cpu_id;
            auto rc = hwloc_set_cpubind(topology.handle(), &cpu_affinity.bitmap(), HWLOC_CPUBIND_THREAD);
            CHECK_NE(rc, -1);
        }

        // todo(ryan) - enable thread/memory binding should be a system option, not specifically a fiber_pool option
        if (options.fiber_pool().enable_memory_binding() and m_cap_sys_nice)
        {
            // use the following cpu_set which may span multiple numa nodes to allow membind across all numa domains in
            // the host partition if enabled, this can relax the fatal condition above where we disable the threads
            // ability to migrate across numa boundaries partitions().host_partition_containing(cpu_affinity).cpu_set()

            DVLOG(10) << info << " hwloc_set_membind to cpus: " << cpu_set.str();
            auto rc = hwloc_set_membind(topology.handle(), &cpu_set.bitmap(), HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_THREAD);
            if (rc == -1)
            {
                LOG(WARNING)
                    << "unable to set memory policy - if using docker use: --cap-add=sys_nice to allow membind";
                m_cap_sys_nice = false;
            }
        }

        // use the first cpu_id in the cpu_affinity to query and execute the thread_local initializers
        // int cpu_id  = cpu_affinity.first();
        // auto result = m_thread_initializers.equal_range(cpu_id);
        // for (auto it = result.first; it != result.second; it++)
        // {
        //     it->second();
        // }
    }
    void deinitialize_thread(const std::string& desc, const Bitmap& cpu_set, const std::size_t& thread_idx)
    {
        DVLOG(10) << "deinitializing thread " << thread_idx << " for " << desc;
    }

    std::map<std::string, std::unique_ptr<TaskQueue<coro::thread_pool>>> m_task_queues;

    // bool flag which if true will attempt to perfom a hwloc_set_membind in the thread_pools' start functor
    // if hwlock_set_membind fails, this variable is set to false so we do not attempt to set this value on
    // future threads.
    // @note if using docker use: --cap-add=sys_nice to allow membind
    std::atomic<bool> m_cap_sys_nice{true};
};

}  // namespace srf::internal::coroutines
