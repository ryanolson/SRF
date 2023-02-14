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
#include "mrc/channel/v2/async_read.hpp"
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/channel/v2/connectors/channel_acceptor.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/component.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/output_stream.hpp"
#include "mrc/ops/cpo/outputs.hpp"
#include "mrc/ops/edge.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>
#include <utility>

namespace mrc::ops {

namespace detail {

template <typename DataT>
struct Output final : public Component
{
  public:
    using data_type = DataT;

    explicit Output(std::size_t tag) :
      m_tag(tag),
      m_shared_state(std::make_shared<coroutines::SymmetricTransfer<DataT>>()),
      m_output_stream(m_shared_state)
    {}

    OutputStream<DataT> output_stream()
    {
        return m_output_stream;
    }

    bool is_connected() const
    {
        return !m_shared_state;
    }

    // initialize should initialize the edge
    // [[nodiscard]] coroutines::Task<> initialize() final
    // {
    //     co_return;
    // }

    [[nodiscard]] coroutines::Task<> start() final
    {
        co_await m_shared_state->wait_until_initialized();
        co_return;
    }

    [[nodiscard]] coroutines::Task<> finalize() final
    {
        m_shared_state->close();
        co_return;
    }

  private:
    void connect_edge(std::shared_ptr<EdgeWritable<DataT>> edge)
    {
        CHECK(!m_edge);
        m_edge = std::move(edge);
    }

    // the returned generator must be passed to an edge so it can be transfered to the downstream scheduling term
    // it is the responsiblity of another operator to execute the generator
    coroutines::AsyncGenerator<DataT> make_direct_generator()
    {
        auto shared_state = std::move(m_shared_state);
        CHECK(shared_state);

        auto generator = [](std::shared_ptr<coroutines::SymmetricTransfer<DataT>> shared_state)
            -> coroutines::AsyncGenerator<DataT> {
            co_await shared_state->initialize();
            while (*shared_state)
            {
                co_yield *(shared_state->data());
                co_await shared_state->async_read();
            }
        };

        return generator(std::move(shared_state));
    }

    // the returned writer should be owned and executed by the current operator
    template <channel::v2::concepts::writable ChannelT>
    coroutines::Task<> make_channel_writer(std::shared_ptr<ChannelT> channel)
    {
        auto shared_state = std::move(m_shared_state);
        CHECK(shared_state);

        auto writer = [](std::shared_ptr<coroutines::SymmetricTransfer<DataT>> shared_state,
                         std::shared_ptr<ChannelT> channel) -> coroutines::Task<> {
            co_await shared_state->initialize();
            while (*shared_state)
            {
                channel::v2::async_write(*channel, (DataT &&) shared_state->data());
                co_await shared_state->async_read();
            }
        };

        return writer(shared_state, channel);
    }

    // the returned writer should be owned and executed by the current operator
    template <channel::v2::concepts::writable ChannelT>
    coroutines::AsyncGenerator<DataT> make_channel_reader(std::shared_ptr<ChannelT> channel)
    {
        while (auto data = co_await channel::v2::async_read(*channel))
        {
            co_yield *data;
        }
    }

    std::size_t m_tag;
    std::shared_ptr<coroutines::SymmetricTransfer<DataT>> m_shared_state;
    OutputStream<DataT> m_output_stream;
    std::shared_ptr<EdgeWritable<DataT>> m_edge{nullptr};
};

template <typename T, typename = typename T::output_type>
class OutputsImpl;

// operators with a single output type and no concurrency method can be generator edges
// operators with multiple outputs can only be connected by channel edges
template <typename OperationT, typename... Types>  // NOLINT
class OutputsImpl<OperationT, std::tuple<Types...>> : public Component
{
    template <std::size_t... I>
    static std::tuple<Output<Types>...> make_outputs(std::index_sequence<I...> indexes)
    {
        return std::tuple<Output<Types>...>{Output<Types>(I)...};
    }

  public:
    using data_type = std::tuple<Types...>;

    OutputsImpl() : m_outputs(make_outputs(std::make_index_sequence<sizeof...(Types)>{})) {}

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return sizeof...(Types);
    }

    [[nodiscard]] coroutines::Task<> initialize() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.initialize()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> start() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.start()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> stop() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.stop()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> complete() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.complete()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> finalize() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.finalize()), ...);
                },
                m_outputs);
        }
        co_return;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_streams> _, OutputsImpl& self)
        -> std::tuple<OutputStream<Types>...>
    requires(sizeof...(Types) > 0)
    {
        return std::apply(
            [&](auto&&... args) {
                return std::make_tuple(args.output_stream()...);
            },
            self.m_outputs);
        ;
    }

    friend auto tag_invoke(unifex::tag_t<cpo::make_output_streams> _, OutputsImpl& self) -> std::tuple<>
    requires(sizeof...(Types) == 0)
    {
        return std::make_tuple();
    }

    std::tuple<Output<Types>...> m_outputs;
};

}  // namespace detail
template <typename T>
using Outputs = detail::OutputsImpl<T>;  // NOLINT

}  // namespace mrc::ops
