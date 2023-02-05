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
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/output_stream.hpp"
#include "mrc/ops/cpo/outputs.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>

namespace mrc::ops {

namespace detail {

template <typename DataT>
struct Output
{
  public:
    using data_type = DataT;

    Output() : m_shared_state(std::make_shared<coroutines::SymmetricTransfer<DataT>>()), m_output_stream(m_shared_state)
    {}

    OutputStream<DataT> output_stream()
    {
        return m_output_stream;
    }

    bool is_connected() const
    {
        return !m_shared_state;
    }

    // this need to get moved to Output
    [[nodiscard]] auto init()
    {
        return m_shared_state->wait_until_initialized();
    }

    [[nodiscard]] auto finalize()
    {
        m_shared_state->close();
        return std::suspend_never{};
    }

    // the returned generator must be passed to an edge so it can be transfered to the downstream scheduling term
    // it is the responsiblity of another operator to execute the generator
    coroutines::AsyncGenerator<DataT> make_direct_generator()
    {
        auto shared_state = std::move(m_shared_state);
        CHECK(shared_state);

        auto generator = [](decltype(shared_state) shared_state) -> coroutines::AsyncGenerator<DataT> {
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

        auto writer = [](decltype(shared_state) shared_state, std::shared_ptr<ChannelT> channel) -> coroutines::Task<> {
            co_await shared_state->initialize();
            while (*shared_state)
            {
                channel::v2::async_write(*channel, (DataT &&) shared_state->data());
                co_await shared_state->async_read();
            }
        };

        return writer(shared_state, channel);
    }

  private:
    std::shared_ptr<coroutines::SymmetricTransfer<DataT>> m_shared_state;
    OutputStream<DataT> m_output_stream;
};

template <typename T, typename = typename T::output_type>
class OutputsImpl;

// operators with a single output type and no concurrency method can be generator edges
// operators with multiple outputs can only be connected by channel edges
template <typename OperationT, typename... Types>  // NOLINT
class OutputsImpl<OperationT, std::tuple<Types...>>
{
  public:
    using data_type = std::tuple<Types...>;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return sizeof...(Types);
    }

    coroutines::Task<std::tuple<OutputStream<Types>...>> init()
    {
        std::apply(
            [](auto&&... outputs) {
                ((co_await outputs.init()), ...);
            },
            m_outputs);

        co_return std::apply(
            [&](auto&&... args) {
                return std::make_tuple(args.output_stream()...);
            },
            m_outputs);
    }

    coroutines::Task<> finalize()
    {
        std::apply(
            [](auto&&... outputs) {
                ((co_await outputs.finalize()), ...);
            },
            m_outputs);
        co_return;
    }

  private:
    std::tuple<Output<Types>...> m_outputs;
};

}  // namespace detail
template <typename T>
using Outputs = detail::OutputsImpl<T>;  // NOLINT

}  // namespace mrc::ops
