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

#include "srf/core/addresses.hpp"
#include "srf/core/runtime.hpp"
#include "srf/manifold/composite_manifold.hpp"
#include "srf/manifold/interface.hpp"
#include "srf/node/edge_builder.hpp"
#include "srf/node/forward.hpp"
#include "srf/node/generic_sink.hpp"
#include "srf/node/operators/muxer.hpp"
#include "srf/node/rx_sink.hpp"
#include "srf/node/source_channel.hpp"
#include "srf/pipeline/resources.hpp"
#include "srf/runnable/context.hpp"
#include "srf/runnable/launch_options.hpp"
#include "srf/runnable/launchable.hpp"
#include "srf/runnable/runnable.hpp"
#include "srf/runnable/types.hpp"
#include "srf/types.hpp"

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>

namespace srf::manifold {

namespace detail {

template <typename T>
class Balancer : public node::GenericSink<T>
{
  public:
    Balancer(RoundRobinEgress<T>& state) : m_state(state) {}

  private:
    void on_data(T&& data) final
    {
        // LOG(INFO) << "balancer node: " << data;
        m_state.await_write(std::move(data));
    }

    void will_complete() final
    {
        DVLOG(10) << "shutdown load-balancer - clear output channels";
        m_state.clear();
    };

    RoundRobinEgress<T>& m_state;
};

template <typename T>
class Balancer2 : public node::SinkChannel<T>, public runnable::RunnableWithContext<>
{
    using state_t = runnable::Runnable::State;

  public:
    Balancer2(RoundRobinEgress<T>& state) : m_state(state) {}

    void on_output_changed() {}

  private:
    void run(runnable::Context& context) override
    {
        T data;

        while (m_is_running && (node::SinkChannel<T>::egress().await_read(data) == channel::Status::success))
        {
            std::unique_lock lock(m_mutex);

            bool has_output = false;

            // Check for output
            m_has_output_cv.wait(lock, [this, &has_output]() {
                has_output = !m_state.output_channels().empty();

                // Block while we have no output
                return has_output;
            });

            if (has_output)
            {
                // Can push output while we have the lock
                m_state.await_write(std::move(data));
            }
            else
            {
                // std::swap(data, T());
            }
        }
    }

    void on_state_update(const state_t& state) override
    {
        switch (state)
        {
        case state_t::Stop:
            m_is_running = false;
            break;

        case state_t::Kill:
            m_is_running = false;
            break;

        default:
            break;
        }
    }

    std::atomic<bool> m_is_running{false};
    boost::fibers::mutex m_mutex;
    boost::fibers::condition_variable m_has_output_cv;

    RoundRobinEgress<T>& m_state;
};

}  // namespace detail

template <typename T>
class LoadBalancer : public CompositeManifold<MuxedIngress<T>, RoundRobinEgress<T>>
{
    using base_t = CompositeManifold<MuxedIngress<T>, RoundRobinEgress<T>>;

  public:
    LoadBalancer(PortName port_name, core::IRuntime& resources) : base_t(std::move(port_name), resources)
    {
        m_launch_options.engine_factory_name = "main";
        m_launch_options.pe_count            = 1;
        m_launch_options.engines_per_pe      = 8;

        // construct any resources
        this->resources()
            .main()
            .enqueue([this] {
                m_balancer = std::make_unique<detail::Balancer<T>>(this->egress());
                node::make_edge(this->ingress().source(), *m_balancer);
            })
            .get();
    }

    void start() final
    {
        this->resources()
            .main()
            .enqueue([this] {
                if (m_runner)
                {
                    // todo(#179) - validate this fix and improve test coverage
                    // this will be handled now by the default behavior of SourceChannel::no_channel method
                    // CHECK(!this->egress().output_channels().empty()) << "no egress channels on manifold";
                    return;
                }
                CHECK(m_balancer);
                m_runner = this->resources()
                               .launch_control()
                               .prepare_launcher(launch_options(), std::move(m_balancer))
                               ->ignition();

                m_runner->on_completion_callback([this](bool ok) {
                    VLOG(10) << "LoadBalancer upstream completed";

                    // Now here, we need to remove any publishers
                    this->set_publisher(nullptr);
                });
            })
            .get();
    }

    void join() final
    {
        m_runner->await_join();
    }

    const runnable::LaunchOptions& launch_options() const
    {
        return m_launch_options;
    }

  private:
    // launch options
    runnable::LaunchOptions m_launch_options;

    // this is the progress engine that will drive the load balancer
    std::unique_ptr<detail::Balancer<T>> m_balancer;

    // runner
    std::unique_ptr<runnable::Runner> m_runner{nullptr};
};

}  // namespace srf::manifold
