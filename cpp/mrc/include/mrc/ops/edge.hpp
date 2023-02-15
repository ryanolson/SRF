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

#include "mrc/channel/v2/async_read.hpp"
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/channel/v2/concepts/readable.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/channel/v2/connectors/channel_provider.hpp"
#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/scheduling_terms/on_next_data.hpp"

#include <coroutine>
#include <map>
#include <mutex>
#include <optional>

namespace mrc::ops {

template <core::concepts::data DataT>
struct EdgeWritable
{
    virtual ~EdgeWritable() = default;

    virtual void set_generator(coroutines::AsyncGenerator<DataT>&& generator) = 0;
};

template <core::concepts::data DataT>
struct EdgeReadable
{
    virtual ~EdgeReadable() = default;

    struct GetOperation
    {
        virtual bool await_ready()                                    = 0;
        virtual void await_suspend(std::coroutine_handle<> coroutine) = 0;
        virtual coroutines::AsyncGenerator<DataT> await_resume()      = 0;
    };

    virtual GetOperation get_generator() = 0;
};

enum class EdgeType
{
    Direct,
    Buffered,
    Recent,
    Immediate,
};

class EdgePlug;
class EdgeSocket;

class EdgePlugWriter;  // Output
class EdgePlugReader;  // Input

class EdgeSocketWriter;
class EdgeSocketReader;

class OutputPlug;
class ChannelSocket;
class ChannelPlug;
class InputSocket;

// plugs connect to sockets
// Output (empty writer task from Input) -> Input (direct generator from Output)
// Ouptut (writer task from Channel) -> Channel -> Input (channel reader generator from Channel)
// sockets need to get generator from the upstream
// plugs might set state on the socket (pass the generator) or get back the writer task from the symmetric transfer

// output plug should always receieve a writer task from the downstream, be it an actual writer an empty task
// the socket should make a cpo call on the plug
// - channel socket makes the get_symxfer call on the OutputPlug, returns the Task<>
// - input socket makes get_generator call on OutputPlug, set the Task which the co_await start() sets internally
// the socket should have a cpo to fetch the task which gets called on plug::start

template <core::concepts::data DataT>
class Edge : public EdgeWritable<DataT>, public EdgeReadable<DataT>
{
  public:
    using data_type = DataT;

    const EdgeType& edge_type() const
    {
        return m_edge_type;
    }

  private:
    template <channel::v2::concepts::concrete_channel_of<DataT> ChannelT>
    void make_generators()
    {
        using namespace mrc::coroutines;
        using namespace mrc::channel::v2;

        auto make_channel_lambdas = [this]<channel::v2::concepts::concrete_writable_of<DataT> WritableT,
                                           channel::v2::concepts::concrete_readable_of<DataT> ReadableT>(
                                        std::shared_ptr<WritableT> writable_channel,
                                        std::shared_ptr<ReadableT> readable_channel) {
            m_make_writer_task_fn =
                [writable_channel](std::shared_ptr<SymmetricTransfer<DataT>> shared_state) -> Task<> {
                co_await shared_state->initialize();
                while (*shared_state)
                {
                    async_write(*writable_channel, std::move(*(shared_state->data())));
                    co_await shared_state->async_read();
                }
            };
            m_make_reader_generator_fn = [readable_channel]() -> AsyncGenerator<DataT> {
                while (auto data = co_await async_read(*readable_channel))
                {
                    co_yield *data;
                }
            };
        };

        switch (m_edge_type)
        {
        case EdgeType::Direct: {
            auto make_task_fn = []() -> Task<> {
                co_return;
            };
            break;
        }
        // case EdgeType::Buffered: {
        //     auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
        //     make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
        //     break;
        // }
        // case EdgeType::Recent: {
        //     auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
        //     make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
        //     break;
        // }
        case EdgeType::Immediate: {
            auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
            make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
            break;
        }
        default:
            LOG(FATAL) << "Unrecognized ChannelType";
        }
    }

    EdgeType m_edge_type{EdgeType::Direct};
    std::function<coroutines::AsyncGenerator<DataT>()> m_make_reader_generator_fn{nullptr};
    std::function<coroutines::Task<>(std::shared_ptr<coroutines::SymmetricTransfer<DataT>>)> m_make_writer_task_fn{
        nullptr};
    std::mutex m_mutex;
};

}  // namespace mrc::ops
