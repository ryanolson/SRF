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

#include "srf/manifold/interface.hpp"
#include "srf/node/edge_builder.hpp"
#include "srf/node/operators/muxer.hpp"
#include "srf/node/sink_properties.hpp"
#include "srf/node/source_properties.hpp"

#include <memory>
#include <mutex>

namespace srf::manifold {

class EgressDelegate
{
  public:
    virtual ~EgressDelegate() = default;

    void add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink);

    void remove_output(const SegmentAddress& address);

  protected:
    boost::fibers::mutex& mutex();
    const std::set<SegmentAddress>& connected_addresses() const;

    virtual void do_add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink) = 0;
    virtual void do_remove_output(const SegmentAddress& address)                                     = 0;

  private:
    boost::fibers::mutex m_mutex;
    std::set<SegmentAddress> m_connected_addresses;
};

template <typename T>
class TypedEngress : public EgressDelegate
{
  public:
    using data_t = T;

  protected:
    void do_add_output(const SegmentAddress& address, node::SinkPropertiesBase* output_sink) final
    {
        auto sink = dynamic_cast<node::SinkProperties<T>*>(output_sink);
        CHECK(sink);
        this->do_add_typed_output(address, *sink);
    }

    virtual void do_add_typed_output(const SegmentAddress& address, node::SinkProperties<T>& output_sink) = 0;
};

template <typename T>
class MappedEgress : public TypedEngress<T>
{
  public:
    using channel_map_t = std::map<SegmentAddress, std::unique_ptr<node::SourceChannelWriteable<T>>>;

    const channel_map_t& output_channels() const
    {
        return m_outputs;
    }

    void clear()
    {
        m_outputs.clear();
    }

  protected:
    void do_add_typed_output(const SegmentAddress& address, node::SinkProperties<T>& sink) override
    {
        auto search = m_outputs.find(address);

        CHECK(search == m_outputs.end()) << "Egress output already added for address: " << address;

        auto output_channel = std::make_unique<node::SourceChannelWriteable<T>>();

        node::make_edge(*output_channel, sink);

        m_outputs[address] = std::move(output_channel);
    }

    void do_remove_output(const SegmentAddress& address) override
    {
        auto search = m_outputs.find(address);

        // Delete the output
        if (search != m_outputs.end())
        {
            m_outputs.erase(address);
        }
    }

  private:
    channel_map_t m_outputs;
};

template <typename T>
class RoundRobinEgress : public MappedEgress<T>
{
  public:
    // todo(#189) - use raw_checks for hot path
    void await_write(T&& data)
    {
        std::unique_lock lock(this->mutex());

        m_has_output_cv.wait(lock, [this]() {
            // Block while we have no output
            return !this->output_channels().empty();
        });

        // CHECK_LT(m_next, m_pick_list.size());
        auto next = m_next++;
        // // roll counter before await_write which could yield
        // if (m_next == m_pick_list.size())
        // {
        //     m_next = 0;
        // }

        CHECK(m_pick_list[next % m_pick_list.size()]->await_write(std::move(data)) == channel::Status::success);
    }

  private:
    void do_add_typed_output(const SegmentAddress& address, node::SinkProperties<T>& sink) override
    {
        MappedEgress<T>::do_add_typed_output(address, sink);
        update_pick_list();

        // Signal the CV to trigger reevaluation
        m_has_output_cv.notify_all();
    }

    void do_remove_output(const SegmentAddress& address) override
    {
        MappedEgress<T>::do_remove_output(address);
        update_pick_list();

        // Signal the CV to trigger reevaluation
        m_has_output_cv.notify_all();
    }

    void update_pick_list()
    {
        m_pick_list.clear();
        m_pick_list.reserve(this->output_channels().size());
        for (const auto& [rank, channel] : this->output_channels())
        {
            m_pick_list.push_back(channel.get());
        }
        std::random_shuffle(m_pick_list.begin(), m_pick_list.end());
        m_next = 0;
    }

    std::size_t m_next{0};
    std::vector<node::SourceChannelWriteable<T>*> m_pick_list;
    boost::fibers::condition_variable m_has_output_cv;
};

}  // namespace srf::manifold
