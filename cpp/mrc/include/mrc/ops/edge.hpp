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
#include "mrc/coroutines/scheduler.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/controller.hpp"
#include "mrc/ops/output.hpp"

#include <coroutine>
#include <map>
#include <mutex>
#include <optional>

namespace mrc::ops {

enum class EdgeType
{
    Direct,
    Buffered,
    Recent,
};

enum class ConnectionType
{
    Direct,
    Buffered,
    Recent,
};

enum class ConnectorType
{
    Connector,
    Connection,
};

template <core::concepts::data T>
class Plug;

template <core::concepts::data T>
class Socket;

struct EdgeBuilder;

class Connector
{};

template <core::concepts::data T>
struct OutputPort
{
    virtual ~OutputPort() = default;

    virtual coroutines::Task<coroutines::Task<>> make_writer(
        std::shared_ptr<coroutines::SymmetricTransfer<T>> shared_state) = 0;
};

template <core::concepts::data T>
class Socket
{
    void set_connection(std::shared_ptr<Plug<T>> connection)
    {
        CHECK(!m_connection);
        // fail if both this and connection are both extensions
        // fail if both this and connector are both extensions
        m_connection = connection;
    }

    std::shared_ptr<Plug<T>> m_connection;
    friend EdgeBuilder;
};

// on start() of both output and input
// - output::start
//   - create shared state
//   - make output_strema
//   - auto writer_task = co_await connection.make_writer_task(shared_state);
//   - ^^^^ may not have to be async - could be a straight sync call ^^^^
// - input::start
//   - co_await connetion.make_async_generator();
//   - ^^^^^^^^ must be async because the writer need to arrive first
//   - form input stream
template <core::concepts::data WriteT, core::concepts::data ReaderT = WriteT>
class Connection final : public OutputPort<WriteT>,
                         public Plug<ReaderT>,
                         public std::enable_shared_from_this<Connection<WriteT, ReaderT>>
{
  public:
    Connection(Output<WriteT>& plug, Socket<ReaderT>& socket) : m_plug(plug), m_socket(socket)
    {
        // determine default connection type
    }

    ConnectorType connection_type() const final
    {
        return ConnectorType::Connection;
    }

    void connect()
    {
        if (connection_type() == ConnectionType::Direct)
        {
            auto shared_state = m_plug.get_shard_state();
            CHECK(shared_state);
            m_socket.set_generator(m_plug.template get_generator<ReaderT>(std::move(shared_state)));
            m_plug.set_writer_task([]() -> coroutines::Task<> {
                co_return;
            }());
        }
        else {}
    }

  private:
    class WriterConnectOperation
    {
      public:
        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting_writer) noexcept
        {
            std::lock_guard lock(m_parent.m_mutex);
            // if downstream reader has suspended, resume it by returning its coroutine handle;
            // otherwise, simply suspend the writer until the reader checks in
            m_awaiting_writer                      = awaiting_writer;
            m_parent.m_writer_connection_operation = this;
        }

        constexpr static void await_resume() noexcept {}

      private:
        WriterConnectOperation(Connection& parent, coroutines::Scheduler& scheduler) :
          m_parent(parent),
          m_scheduler(scheduler)
        {}

        Connection& m_parent;
        coroutines::Scheduler& m_scheduler;
        std::coroutine_handle<> m_awaiting_writer;
    };

    class ReaderConnectOperation
    {
      public:
        bool await_ready() noexcept
        {
            m_lock = std::unique_lock(m_parent.m_mutex);
            if (m_parent.m_writer_connect_operation != nullptr)
            {
                CHECK(m_parent.m_generator);
                auto lock = std::move(m_lock);
                return true;
            }
            // lock is still owned
            return false;
        }

        void await_suspend(std::coroutine_handle<> awaiting_reader)
        {
            auto lock = std::move(m_lock);
            CHECK(m_parent.m_reader_connect_operation == nullptr);

            m_awaiting_reader                   = awaiting_reader;
            m_parent.m_reader_connect_operation = this;
        }

        auto await_resume() noexcept -> coroutines::AsyncGenerator<ReaderT>
        {
            return std::move(*m_parent.m_generator);
        }

      private:
        ReaderConnectOperation(Connection& parent, coroutines::Scheduler& scheduler) :
          m_parent(parent),
          m_scheduler(scheduler)
        {}

        Connection& m_parent;
        coroutines::Scheduler& m_scheduler;
        std::coroutine_handle<> m_awaiting_reader;
        std::unique_lock<std::mutex> m_lock;
    };

    coroutines::Task<coroutines::Task<>> make_writer(
        std::shared_ptr<coroutines::SymmetricTransfer<WriteT>> shared_state) final
    {}

    coroutines::AsyncGenerator<ReaderT> direct_generator(
        std::shared_ptr<coroutines::SymmetricTransfer<WriteT>> shared_state)
    {
        co_await shared_state->initialize();
        if constexpr (std::same_as<WriteT, ReaderT>)
        {
            while (shared_state->data())
            {
                co_yield *(shared_state->data());
                co_await shared_state->async_read();
            }
        }
        else
        {
            ReaderT u;
            while (shared_state->data())
            {
                u = *(shared_state->data());
                co_yield u;
                co_await shared_state->async_read();
            }
        }
    }

    void init_direct()
    {
        using namespace mrc::coroutines;

        auto writer = []() -> Task<> {
            co_return;
        };

        auto generator = [](std::shared_ptr<SymmetricTransfer<WriteT>> shared_state) {
            co_await shared_state->initialize();
            if constexpr (std::same_as<WriteT, ReaderT>)
            {
                while (shared_state->data())
                {
                    co_yield *(shared_state->data());
                    co_await shared_state->async_read();
                }
            }
            else
            {
                ReaderT cast;
                while (shared_state->data())
                {
                    cast = *(shared_state->data());
                    co_yield cast;
                    co_await shared_state->async_read();
                }
            }
        };

        m_writer    = writer();
        m_generator = generator(m_shared_state);
    }

    void init()
    {
        using namespace mrc::coroutines;
        using namespace mrc::channel::v2;

        auto builder = [this]<channel::v2::concepts::concrete_writable_of<ReaderT> WritableT,
                              channel::v2::concepts::concrete_readable_of<ReaderT> ReadableT>(
                           std::shared_ptr<WritableT> writable_channel,
                           std::shared_ptr<ReadableT> readable_channel) {
            auto writer = [](std::shared_ptr<SymmetricTransfer<WriteT>> shared_state,
                             std::shared_ptr<WritableT> writable_channel) -> coroutines::Task<> {
                co_await shared_state->initialize();
                while (*shared_state)
                {
                    if constexpr (std::same_as<WriteT, ReaderT>)
                    {
                        co_await async_write(*writable_channel, std::move(*(shared_state->data())));
                    }
                    else
                    {
                        ReaderT cast = *(shared_state->data());
                        co_await async_write(*writable_channel, std::move(cast));
                    }
                    co_await shared_state->async_read();
                }
            };

            auto generator = [](std::shared_ptr<ReadableT> readable_channel) -> coroutines::AsyncGenerator<ReaderT> {
                while (auto data = co_await async_read(*readable_channel))
                {
                    co_yield *data;
                }
            };

            m_writer    = writer(m_shared_state, writable_channel);
            m_generator = generator(readable_channel);
        };

        switch (m_connection_type)
        {
        case ConnectionType::Direct: {
            init_direct();
            break;
        }

        // todo(ryan) - specialize buffer channel to a SPSC (single producer / single consumer) ring buffer
        case ConnectionType::Buffered: {
            auto provider = make_channel_provider(std::make_unique<ImmediateChannel<ReaderT>>());
            builder(provider->writable_channel(), provider->readable_channel());
            break;
        }

        // todo(ryan) - specialize recent channel to a SPSC (single producer / single consumer) ring buffer
        case ConnectionType::Recent: {
            auto provider = make_channel_provider(std::make_unique<ImmediateChannel<ReaderT>>());
            builder(provider->writable_channel(), provider->readable_channel());
            break;
        }

        default:
            LOG(FATAL) << "Unrecognized ChannelType";
        }
    }

    Plug<WriteT>& m_plug;
    Socket<ReaderT>& m_socket;
    ConnectionType m_connection_type;
    std::shared_ptr<coroutines::SymmetricTransfer<WriteT>> m_shared_state;
    std::optional<coroutines::Task<>> m_writer;
    std::optional<coroutines::AsyncGenerator<ReaderT>> m_generator;

    WriterConnectOperation* m_writer_connect_operation{nullptr};
    ReaderConnectOperation* m_reader_connect_operation{nullptr};
    std::mutex m_mutex;

    friend WriterConnectOperation;
};

template <core::concepts::data T, core::concepts::data U = T>
class Edge : public Component
{
    coroutines::Task<> disconnect()
    {
        // m_plug.controller().set_requested_state(RequestedState::Pause);
        // m_socket.controller().set_requested_state(RequestedState::Pause);

        // co_await coroutines::when_all(m_plug.controller().wait_until(AchievedState::NotRunning),
        //                               m_socket.controller().wait_until(AchievedState::NotRunning));

        // m_plug.disconnect();
        // m_socket.disconnect();
        co_return;
    }

    Plug<T>& m_plug;
    Socket<U>& m_socket;
    friend EdgeBuilder;
};

struct EdgeBuilder
{
    template <typename T, typename U>
    static void make_connection(Plug<T>& plug, Socket<U>& socket)
    {
        if (plug.connected())
        {
            throw std::runtime_error("plug is already connected");
        }

        if (socket.connected())
        {
            throw std::runtime_error("socket is already connected");
        }

        // connector come in two types:
        // - standard
        // - extension

        // a socket can either accept a generator or provide a writer task
        if (socket.accepts_generator())
        {
            socket.set_generator(plug.get_generator());
        }
        else if (socket.provides_writer_task())  // this is an adapter
        {
            plug.set_writer_task(socket.get_writer_task(plug.get_shared_state()));
        }
        else {}
    }
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

// Input connected directly to Output
// - start
//   - co_await output_plug.get_reader_generator(*this);

// Output connected directly to Input
// - start
//   - co_await input_socket.get_writer_task(*this); // empty task

// Output connected directly to Channel
// - start
//   - co_await channel_socket.get_writer_task(*this)
//     - channel_socket will need to access this/output/plug::m_symmetric_tranfer which is private

// template <core::concepts::data DataT>
// class Edge : public EdgeWritable<DataT>, public EdgeReadable<DataT>
// {
//   public:
//     using data_type = DataT;

//     const EdgeType& edge_type() const
//     {
//         return m_edge_type;
//     }

//   private:
//     template <channel::v2::concepts::concrete_channel_of<DataT> ChannelT>
//     void make_generators()
//     {
//         using namespace mrc::coroutines;
//         using namespace mrc::channel::v2;

//         auto make_channel_lambdas = [this]<channel::v2::concepts::concrete_writable_of<DataT> WritableT,
//                                            channel::v2::concepts::concrete_readable_of<DataT> ReadableT>(
//                                         std::shared_ptr<WritableT> writable_channel,
//                                         std::shared_ptr<ReadableT> readable_channel) {
//             m_make_writer_task_fn =
//                 [writable_channel](std::shared_ptr<SymmetricTransfer<DataT>> shared_state) -> Task<> {
//                 co_await shared_state->initialize();
//                 while (*shared_state)
//                 {
//                     async_write(*writable_channel, std::move(*(shared_state->data())));
//                     co_await shared_state->async_read();
//                 }
//             };
//             m_make_reader_generator_fn = [readable_channel]() -> AsyncGenerator<DataT> {
//                 while (auto data = co_await async_read(*readable_channel))
//                 {
//                     co_yield *data;
//                 }
//             };
//         };

//         switch (m_edge_type)
//         {
//         case EdgeType::Direct: {
//             auto make_task_fn = []() -> Task<> {
//                 co_return;
//             };
//             break;
//         }
//         // case EdgeType::Buffered: {
//         //     auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
//         //     make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
//         //     break;
//         // }
//         // case EdgeType::Recent: {
//         //     auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
//         //     make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
//         //     break;
//         // }
//         case EdgeType::Immediate: {
//             auto provider = make_channel_provider(std::make_unique<ImmediateChannel<DataT>>());
//             make_channel_lambdas(provider->writable_channel(), provider->readable_channel());
//             break;
//         }
//         default:
//             LOG(FATAL) << "Unrecognized ChannelType";
//         }
//     }

//     EdgeType m_edge_type{EdgeType::Direct};
//     std::function<coroutines::AsyncGenerator<DataT>()> m_make_reader_generator_fn{nullptr};
//     std::function<coroutines::Task<>(std::shared_ptr<coroutines::SymmetricTransfer<DataT>>)> m_make_writer_task_fn{
//         nullptr};
//     std::mutex m_mutex;
// };

}  // namespace mrc::ops
