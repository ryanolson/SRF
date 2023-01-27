/**
 * SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/channel/v2/connectors/channel_acceptor.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>

namespace mrc::ops {

struct SingleOutput
{};

struct MultipleOutputs
{};

template <typename T>
class OutputStream
{
  public:
    explicit OutputStream(std::shared_ptr<coroutines::SymmetricTransfer<T>> shared_state) :
      m_shared_state(std::move(shared_state))
    {
        CHECK(m_shared_state);
    }

    ~OutputStream()
    {
        m_shared_state->close();
    }

    [[nodiscard]] auto async_write(T&& data) noexcept
    {
        return m_shared_state->async_write(std::move(data));
    }

    [[nodiscard]] auto async_initialized()
    {
        return m_shared_state->reader_initialized();
    }

  private:
    const std::shared_ptr<coroutines::SymmetricTransfer<T>> m_shared_state;
};

// Output is a conduit for passing the OutputStream thru
template <typename T>
class Output : public SingleOutput
{
  public:
    using data_type   = T;
    using output_type = Output<T>;

  private:
    // writable channel of T
    template <typename ChannelT>
    std::pair<OutputStream<T>, coroutines::Task<>> make_channel_writer(std::shared_ptr<ChannelT> channel)
    {
        auto shared_state = std::make_shared<coroutines::SymmetricTransfer<T>>();

        auto channel_writer = [](std::shared_ptr<coroutines::SymmetricTransfer<T>> shared_state,
                                 std::shared_ptr<ChannelT> channel) -> coroutines::Task<> {
            co_await shared_state->initialize_reader();
            while (*shared_state)
            {
                co_await channel::v2::async_write(*channel, std::move(*shared_state->data()));
                co_await shared_state->async_read();
            }
            co_return;
        };

        return std::make_pair({shared_state}, channel_write(shared_state, channel));
    }

    std::pair<OutputStream<T>, coroutines::AsyncGenerator<T>> make_direct_generator()
    {
        auto shared_state = std::make_shared<coroutines::SymmetricTransfer<T>>();

        auto generator =
            [](std::shared_ptr<coroutines::SymmetricTransfer<T>> shared_state) -> coroutines::AsyncGenerator<T> {
            co_await shared_state->initalize_reader();
            while (*shared_state)
            {
                co_yield *(shared_state->data());
                co_await shared_state->async_read();
            }
            co_return;
        };

        return std::make_pair({shared_state}, generator(shared_state));
    }

    template <typename OperationT, typename SchedulingT>
    friend class Operator;
};

struct Vertex
{};

template <typename T>
class Edge
{
  public:
    using data_type = T;

    void attach_reader(std::shared_ptr<Input<T>> input)
    {
        m_readers.push_back(input.set_reader(make_reader()));
    }
    void attach_writer(std::shared_ptr<Output<T>> output)
    {
        m_writers.push_back(output.set_writer(make_writer()));
    }

  private:
    virtual std::pair<OutputStream<data_type>, coroutines::Task<>> make_writer() = 0;
    virtual coroutines::AsyncGenerator<data_type> make_reader()                  = 0;

    std::vector<std::shared_ptr<Vertex>> m_readers;
    std::vector<std::shared_ptr<Vertex>> m_writers;
};

template <typename ChannelT>
class ChannelEdge : public Edge<typename ChannelT::data_type>
{
  public:
    using data_type = typename ChannelT::data_type;

    ChannelEdge(std::unique_ptr<ChannelT> channel);

    void connect_writer(std::shared_ptr<Output<data_type>> output)
    {
        set_output(output);
    }

    void connect_reader(std::shared_ptr<Input<data_type>> input)
    {
        set_input(input);
    }

  private:
    std::pair<OutputStream<data_type>, coroutines::Task<>> make_channel_writer(std::shared_ptr<ChannelT> channel)
    {
        auto shared_state = std::make_shared<coroutines::SymmetricTransfer<data_type>>();

        auto channel_writer = [](std::shared_ptr<coroutines::SymmetricTransfer<data_type>> shared_state,
                                 std::shared_ptr<ChannelT> channel) -> coroutines::Task<> {
            // suspends this task and decrements the latch holding the writer back
            co_await shared_state->initialize_reader();

            // while the shared state's data pointer is not null
            while (*shared_state)
            {
                // write the data to the channel
                co_await channel::v2::async_write(*channel, std::move(*shared_state->data()));

                // then await for the data pointer to be modified by the writer
                co_await shared_state->async_read();
            }
            co_return;
        };

        return std::make_pair({shared_state}, channel_writer(shared_state, channel));
    }

    coroutines::AsyncGenerator<data_type> make_channel_reader()
    {
        auto channel_reader = [](std::shared_ptr<ChannelT> channel) -> coroutines::AsyncGenerator<data_type> {
            while (auto data = co_await channel->async_read())
            {
                co_yield *data;
            }
        };

        return channel_reader(m_channel);
    }

    // lazily construct the edge
    void do_make_edge() final
    {
        // set the output_stream and the task that drives the writer
        // output().set_output_stream(make_output_stream());

        //
        // input().set_input_stream(make_input_stream());
    }

    std::shared_ptr<ChannelT> m_channel;
};

template <typename T>
class DirectEdge : public Edge<T>
{
  public:
    // single_output_operation -> concurrency = 1
    void connect_writer(std::shared_ptr<Output<T>> output)
    {
        // set_output(output);
        // m_output = output;
    }

    // operation with concurrency = 1
    void connect_reader(std::shared_ptr<Input<T>> input)
    {
        // m_input = input;
    }

  private:
    void do_make_edge() final
    {
        // input().set_input_stream(output().make_input_stream());
    }
};

// template<typename... Types>
// class OutputStreams : private std::tuple<OutputStream<Types>...>
// {

// };

// template <typename... Types>  // NOLINT
// struct Outputs : private std::tuple<Output<Types>...>
// {
//     using output_type = Outputs<Types...>;

//     // template <std::size_t Id>
//     // auto& get_output()
//     // {
//     //     return std::get<Id>(*this);
//     // }

//     private:
//     OutputStreams<Types...> make_streams();
// };

}  // namespace mrc::ops
