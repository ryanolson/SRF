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
#include "srf/types.hpp"

#include <memory>

namespace srf::manifold {

class IngressDelegate
{
  public:
    virtual ~IngressDelegate() = default;

    void add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source);

    void remove_input(const SegmentAddress& address);

  protected:
    const std::set<SegmentAddress>& connected_addresses() const;

    virtual node::SinkPropertiesBase& source_base() = 0;

    virtual void do_add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source) = 0;
    virtual void do_remove_input(const SegmentAddress& address)                                        = 0;

  private:
    std::set<SegmentAddress> m_connected_addresses;
};

template <typename T>
class TypedIngress : public IngressDelegate
{
  public:
    using data_t = T;

    node::SourceProperties<T>& source()
    {
        auto sink = dynamic_cast<node::SourceProperties<T>*>(&this->source_base());
        CHECK(sink);
        return *sink;
    }

  protected:
    void do_add_input(const SegmentAddress& address, node::SourcePropertiesBase* input_source) override
    {
        auto source = dynamic_cast<node::SourceProperties<T>*>(input_source);
        CHECK(source);
        do_add_typed_input(address, *source);
    }

    virtual void do_add_typed_input(const SegmentAddress& address, node::SourceProperties<T>& source) = 0;
};

template <typename T>
class MuxedIngress : public TypedIngress<T>
{
  public:
    MuxedIngress() : m_muxer(std::make_shared<node::Muxer<T>>()) {}

  protected:
    void do_add_typed_input(const SegmentAddress& address, node::SourceProperties<T>& source) final
    {
        CHECK(m_muxer);

        node::make_edge(source, *m_muxer);
    }

    void do_remove_input(const SegmentAddress& address) final
    {
        CHECK(m_muxer);

        // Do nothing for now
    }

  private:
    node::SinkPropertiesBase& source_base() final
    {
        return *m_muxer;
    }

    std::shared_ptr<node::Muxer<T>> m_muxer;
};

}  // namespace srf::manifold
