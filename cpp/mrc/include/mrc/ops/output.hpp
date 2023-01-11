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
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>

namespace mrc::ops {

// namespace detail {

// template <channel::v2::concepts::writable ChannelT>
// class OutputImpl : public channel::v2::ChannelAcceptor<ChannelT>
// {
//   protected:
//     inline auto async_write(typename ChannelT::data_type&& data) noexcept -> decltype(auto)
//     {
//         return channel::v2::async_write(this->channel(), std::move(data));
//     }
// };

// }  // namespace detail

// template <typename T>
// struct Output;

// // template specialization for concrete channel types
// template <channel::v2::concepts::writable ChannelT>
// struct Output<ChannelT> : public detail::OutputImpl<ChannelT>
// {};

// // template specialization for data types
// template <std::movable DataT>
// struct Output<DataT> : public detail::OutputImpl<channel::v2::IWritableChannel<DataT>>
// {};

// template <typename... Types>  // NOLINT
// struct Outputs : private std::tuple<Output<Types>...>
// {
//     using output_type = Outputs<Types...>;

//     template <std::size_t Id>
//     auto& get_output()
//     {
//         return std::get<Id>(*this);
//     }
// };

template <typename T>
class Output
{
    class InitialOperation;
    class SwapOperation;

  public:
    using data_type = std::decay_t<T>;

    [[nodiscard]] SwapOperation async_write(data_type&& data) noexcept
    {
        m_data = std::addressof(data);
        return SwapOperation{*this};
    }

  private:
    [[nodiscard]] InitialOperation init() noexcept
    {
        return {*this};
    }

    [[nodiscard]] SwapOperation async_read() noexcept
    {
        return SwapOperation{*this};
    }

    void close() noexcept
    {
        m_data = nullptr;
        m_coroutine.resume();
    }

    class InitialOperation
    {
      public:
        InitialOperation(Output& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> reader) noexcept
        {
            m_parent.m_coroutine = reader;
        }

        constexpr static void await_resume() noexcept {}

      private:
        Output& m_parent;
    };

    class SwapOperation
    {
      public:
        SwapOperation(Output& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> reader) noexcept
        {
            return std::exchange(m_parent.m_coroutine, reader);
        }

        constexpr static void await_resume() noexcept {}

      private:
        Output& m_parent;
    };

    data_type* m_data{nullptr};
    std::coroutine_handle<> m_coroutine;

    template <concepts::operable OperationT, concepts::schedulable SchedulingT>
    friend class Operator;
};

template <typename ChannelT>
class ChannelWriter : public Output<typename ChannelT::data_type>
{
    coroutines::Task<> on_write_task()
    {
        co_await this->init();
        while (this->m_data != nullptr)
        {
            co_await async_write(*m_channel, std::move(*this->m_data));
            co_await this->async_read();
        }
    }

    std::shared_ptr<ChannelT> m_channel;
};

template <typename ChannelT>
class OutputChannel
{
  public:
    using data_type = typename ChannelT::data_type;

  private:
    struct InitializeOperation
    {
        InitializeOperation(OutputChannel& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> coroutine)
        {
            m_parent.m_coroutine = coroutine;
        }

        constexpr static void await_resume() noexcept {}

        OutputChannel& m_parent;
    };

    coroutines::Task<void> make_output_task()
    {
        co_await InitializeOperation{this};
        while (m_data != nullptr)
        {
            co_await async_write(*m_channel, std::move(*m_data));
            co_await std::suspend_always{};
        }
    }

    data_type* m_data{nullptr};
    std::shared_ptr<ChannelT> m_channel;
    std::coroutine_handle<> m_coroutine;
};

template <typename T>
class DirectHandoff
{
  public:
    using data_type = std::decay_t<T>;

    class Writer
    {
      public:
        using data_type = std::decay_t<T>;

        Writer(DirectHandoff& parent) : m_parent(parent) {}

        void write(data_type&& data)
        {
            m_parent.m_pointer = std::addressof(data);
            m_parent.m_downstream.resume();
        }

      private:
        DirectHandoff& m_parent;
    };

    struct InitialReadOp
    {
        InitialReadOp(DirectHandoff& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> reader)
        {
            m_parent.m_downstream = reader;
        }

        constexpr static void await_resume() noexcept {}

        DirectHandoff& m_parent;
    };

    coroutines::Task<> get_reader_task()
    {
        co_await InitialReadOp{*this};
        while (m_pointer != nullptr)
        {
            // do something with the value
            co_await std::suspend_always{};
        }
        co_return;
    }

    void close()
    {
        m_pointer = nullptr;
        m_downstream.resume();
    }

    auto get_writer()
    {
        return Writer{*this};
    }

    std::coroutine_handle<> m_downstream;
    data_type* m_pointer;
};

}  // namespace mrc::ops
