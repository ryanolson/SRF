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

template <typename OperationT, typename SchedulingT>
class Operator;

namespace detail {

template <typename T, typename = typename T::output_type>
struct Outputs;

}

template <typename DataT>
struct Output
{
  public:
    using data_type = DataT;

    Output() : m_shared_state(std::make_shared<coroutines::SymmetricTransfer<DataT>>()), m_output_stream(m_shared_state)
    {}

    /**
     * @brief Initialize the shared SymmetricTransfer object
     * @note This method will suspend the caller until the direct generator or channel writer are initialized. this has
     * the benefit of ensuring the downstream operator is started before the upstream operators.
     */
    [[nodiscard]] auto wait_until_initialized()
    {
        return m_shared_state->wait_until_initialized();
    }

    OutputStream<DataT> output_stream()
    {
        return m_output_stream;
    }

    bool is_connected() const
    {
        return !m_shared_state;
    }

  protected:
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

namespace detail {

// sinks have no outputs
template <concepts::has_output_type_of<void> OperationT>
class Outputs<OperationT>
{
  public:
    using data_type = void;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return 0;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs) -> std::tuple<>
    {
        return std::make_tuple();
    }
};

// operators that have a single output and are not parallel operators
// can be connected via channel edges or passthru edges
// direct generators require both single output and not parallel
template <concepts::has_single_output_type OperationT>
class Outputs<OperationT>
{
  public:
    using data_type = typename OperationT::output_type;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return 1;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs)
        -> std::tuple<OutputStream<data_type>>
    {
        return std::make_tuple(outputs.m_output.output_stream());
    }

    Output<data_type> m_output;
};

// operators with multiple outputs can only be connected by channel edges
// mandatory - each output must be attached to a channel edge
template <concepts::has_multi_output_type OperationT, typename... Types>  // NOLINT
class Outputs<OperationT, std::tuple<Types...>>
{
  public:
    using data_type = std::tuple<Types...>;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return sizeof...(Types);
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs)
        -> std::tuple<OutputStream<Types>...>
    {
        return std::apply([&](auto&&... args) { return std::make_tuple(args.output_stream()...); }, outputs.m_outputs);
    }

    std::tuple<Output<Types>...> m_outputs;
};

}  // namespace detail
template <typename T>
using Outputs = detail::Outputs<T>;  // NOLINT

}  // namespace mrc::ops
