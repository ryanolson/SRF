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

#include "mrc/channel/v2/async_read.hpp"
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/channel.hpp"
#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/generator.hpp"
#include "mrc/coroutines/sync_wait.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/coroutines/when_all.hpp"
#include "mrc/ops/handoff.hpp"

#include <benchmark/benchmark.h>

#include <coroutine>
#include <exception>
#include <stdexcept>

using namespace mrc;

static void mrc_coro_create_single_task_and_sync(benchmark::State& state)
{
    auto task = []() -> coroutines::Task<void> { co_return; };

    for (auto _ : state)
    {
        coroutines::sync_wait(task());
    }
}

static void mrc_coro_create_single_task_and_sync_on_when_all(benchmark::State& state)
{
    auto task = []() -> coroutines::Task<void> { co_return; };

    for (auto _ : state)
    {
        coroutines::sync_wait(coroutines::when_all(task()));
    }
}

static void mrc_coro_create_two_tasks_and_sync_on_when_all(benchmark::State& state)
{
    auto task = []() -> coroutines::Task<void> { co_return; };

    for (auto _ : state)
    {
        coroutines::sync_wait(coroutines::when_all(task(), task()));
    }
}

static void mrc_coro_await_suspend_never(benchmark::State& state)
{
    auto task = [&]() -> coroutines::Task<void> {
        for (auto _ : state)
        {
            co_await std::suspend_never{};
        }
        co_return;
    };

    coroutines::sync_wait(task());
}

// // not-thread safe awaitable that returns a value
// // this is an always ready non-yielding awaitable and should perform
// // similar to a function call with the construction of the awaiter on the stack
// class IncrementingAwaitable : public runnable::v2::SchedulingTerm<std::size_t, int>
// {
//     using scheduling_type = runnable::v2::SchedulingTerm<std::size_t, int>;

//     std::size_t m_counter{0};

//     struct Awaiter
//     {
//         constexpr Awaiter(IncrementingAwaitable& parent) : m_parent(parent) {}

//         constexpr static bool await_ready() noexcept
//         {
//             return true;
//         }

//         constexpr static void await_suspend(std::coroutine_handle<> handle){};

//         scheduling_type::return_type await_resume() noexcept
//         {
//             return {++m_parent.m_counter};
//         }

//         IncrementingAwaitable& m_parent;
//     };

//   public:
//     [[nodiscard]] Awaiter operator co_await() noexcept
//     {
//         return {*this};
//     }
// };

// static_assert(runnable::v2::concepts::scheduling_type<IncrementingAwaitable>);

// static void mrc_coro_await_incrementing_awaitable(benchmark::State& state)
// {
//     IncrementingAwaitable awaitable;
//     auto task = [&]() -> coroutines::Task<void> {
//         std::size_t i;
//         for (auto _ : state)
//         {
//             benchmark::DoNotOptimize(i = *(co_await awaitable));
//         }
//         co_return;
//     };

//     coroutines::sync_wait(task());
// }

// static void mrc_coro_await_incrementing_awaitable_baseline(benchmark::State& state)
// {
//     auto task = [&]() -> coroutines::Task<void> {
//         std::size_t i{0};
//         std::size_t j{0};
//         for (auto _ : state)
//         {
//             benchmark::DoNotOptimize(i = ++j);
//         }
//         co_return;
//     };

//     coroutines::sync_wait(task());
// }

// static void mrc_coro_schedule_then_operate(benchmark::State& state)
// {
//     std::size_t i{0};
//     IncrementingAwaitable scheduling_term;

//     auto operation = [&](size_t& data) -> coroutines::Task<void> {
//         i += data;
//         co_return;
//     };

//     auto task = [&]() -> coroutines::Task<void> {
//         for (auto _ : state)
//         {
//             auto data = co_await scheduling_term;
//             if (!data)
//             {
//                 break;
//             }
//             co_await operation(*data);
//         }
//         co_return;
//     };

//     coroutines::sync_wait(task());
// }

static void mrc_coro_immediate_channel(benchmark::State& state)
{
    channel::v2::ImmediateChannel<std::size_t> immediate_channel;

    auto src = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            co_await immediate_channel.async_write(42);
        }
        immediate_channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto val = co_await immediate_channel.async_read()) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}

static void mrc_coro_immediate_channel_cpo(benchmark::State& state)
{
    channel::v2::ImmediateChannel<std::size_t> immediate_channel;

    auto src = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            co_await channel::v2::cpo::async_write(immediate_channel, 42UL);
        }
        immediate_channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto val = co_await channel::v2::cpo::async_read(immediate_channel)) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}

static void mrc_coro_immediate_channel_any(benchmark::State& state)
{
    channel::v2::ImmediateChannel<std::size_t> immediate_channel;

    auto src = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            co_await channel::v2::async_write(immediate_channel, 42UL);
        }
        immediate_channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto val = co_await channel::v2::async_read(immediate_channel)) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}

static void mrc_coro_immediate_channel_task(benchmark::State& state)
{
    channel::v2::ImmediateChannel<std::size_t> immediate_channel;

    auto src = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            co_await immediate_channel.write_task(42);
        }
        immediate_channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto val = co_await immediate_channel.read_task()) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}

static void mrc_coro_generator(benchmark::State& state)
{
    auto src = [&]() -> coroutines::Generator<int64_t> {
        int64_t i{0};
        for (auto _ : state)
        {
            ++i;
            co_yield i;
        }
    };

    for (const auto& v_1 : src()) {}
}

static void mrc_coro_async_generator(benchmark::State& state)
{
    auto src = [&]() -> coroutines::AsyncGenerator<int64_t> {
        int64_t i{0};
        for (auto _ : state)
        {
            ++i;
            co_yield i;
        }
    };

    auto sink = [&]() -> coroutines::Task<> {
        auto gen = src();
        auto it  = co_await gen.begin();
        while (it != gen.end())
        {
            co_await ++it;
        };
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink()));
}

static void mrc_coro_handoff(benchmark::State& state)
{
    ops::Handoff<std::size_t> channel;

    auto src = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            co_await channel.write(42);
        }
        channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto val = co_await channel.read()) {}
        co_return;
    };

    coroutines::sync_wait(coroutines::when_all(sink(), src()));
}

// static void mrc_coro_generator_driven_immediate_channel(benchmark::State& state)
// {
//     channel::v2::ImmediateChannel<std::size_t> immediate_channel;

//     auto src = [&]() -> coroutines::Generator<std::size_t> {
//         std::size_t data;
//         for (auto _ : state)
//         {
//             co_yield data;
//             co_await immediate_channel.async_write(std::move(data));
//         }
//     };

//     for (const auto& v_1 : src()) {}

//     auto src = [&]() -> coroutines::Task<> {
//         for (auto _ : state)
//         {
//             co_await immediate_channel.async_write(42);
//         }
//         immediate_channel.close();
//         co_return;
//     };

//     auto sink = [&]() -> coroutines::Task<> {
//         while (auto val = co_await immediate_channel.async_read()) {}
//         co_return;
//     };

//     coroutines::sync_wait(coroutines::when_all(sink(), src()));
// }

static auto bar(std::size_t i) -> std::size_t
{
    return i += 5;
}

static void foo(std::size_t i)
{
    benchmark::DoNotOptimize(bar(i));
}

static void mrc_coro_immedate_channel_composite_fn_baseline(benchmark::State& state)
{
    auto task = [&]() -> coroutines::Task<> {
        for (auto _ : state)
        {
            foo(42);
        }
        co_return;
    };

    coroutines::sync_wait(task());
}

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

static void mrc_coro_direct_handoff(benchmark::State& state)
{
    DirectHandoff<std::size_t> channel;

    auto writer = channel.get_writer();

    auto src = [&]() -> coroutines::Task<> {
        std::size_t value = 42;
        for (auto _ : state)
        {
            writer.write(42);
        }
        channel.close();
        co_return;
    };

    auto sink = channel.get_reader_task();

    coroutines::sync_wait(coroutines::when_all(std::move(sink), src()));
}

static void mrc_op_scenario_1(benchmark::State& state)
{
    auto src = [&]() -> coroutines::Generator<std::size_t> {
        int64_t i{0};
        for (auto _ : state)
        {
            ++i;
            co_yield i;
        }
    };

    DirectHandoff<std::size_t> handoff;
    auto writer = handoff.get_writer();
    channel::v2::ImmediateChannel<std::size_t> channel;

    auto writer_task = [&]() -> coroutines::Task<> {
        co_await DirectHandoff<std::size_t>::InitialReadOp{handoff};
        while (handoff.m_pointer != nullptr)
        {
            co_await channel.async_write(std::move(*handoff.m_pointer));
            co_await std::suspend_always{};
        }
        co_return;
    };

    auto eval = [&](std::size_t& data) -> coroutines::Task<> {
        writer.write(std::move(data));
        co_return;
    };

    auto loop = [&]() -> coroutines::Task<> {
        for (auto data : src())
        {
            co_await eval(data);
        }
        handoff.close();
        channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto data = co_await channel.async_read()) {}
    };

    coroutines::sync_wait(coroutines::when_all(writer_task(), sink(), loop()));
}

static void mrc_op_scenario_2(benchmark::State& state)
{
    auto src = [&]() -> coroutines::Generator<std::size_t> {
        int64_t i{0};
        for (auto _ : state)
        {
            ++i;
            co_yield i;
        }
    };

    DirectHandoff<std::size_t> handoff;
    auto writer = handoff.get_writer();
    channel::v2::ImmediateChannel<std::size_t> channel;

    auto writer_task = [&]() -> coroutines::Task<> {
        co_await DirectHandoff<std::size_t>::InitialReadOp{handoff};
        while (handoff.m_pointer != nullptr)
        {
            co_await channel.async_write(std::move(*handoff.m_pointer));
            co_await std::suspend_always{};
        }
        co_return;
    };

    auto eval = [&](std::size_t& data) -> coroutines::Task<> {
        writer.write(std::move(data));
        co_return;
    };

    auto loop = [&]() -> coroutines::Task<> {
        for (auto data : src())
        {
            writer.write(std::move(data));
        }
        handoff.close();
        channel.close();
        co_return;
    };

    auto sink = [&]() -> coroutines::Task<> {
        while (auto data = co_await channel.async_read()) {}
    };

    coroutines::sync_wait(coroutines::when_all(writer_task(), sink(), loop()));
}

BENCHMARK(mrc_coro_create_single_task_and_sync);
BENCHMARK(mrc_coro_create_single_task_and_sync_on_when_all);
BENCHMARK(mrc_coro_create_two_tasks_and_sync_on_when_all);
BENCHMARK(mrc_coro_await_suspend_never);
// BENCHMARK(mrc_coro_await_incrementing_awaitable_baseline);
// BENCHMARK(mrc_coro_await_incrementing_awaitable);
// BENCHMARK(mrc_coro_schedule_then_operate);
BENCHMARK(mrc_coro_immediate_channel);
BENCHMARK(mrc_coro_immediate_channel_cpo);
BENCHMARK(mrc_coro_immediate_channel_any);
BENCHMARK(mrc_coro_immediate_channel_task);
BENCHMARK(mrc_coro_immedate_channel_composite_fn_baseline);
BENCHMARK(mrc_coro_generator);
BENCHMARK(mrc_coro_async_generator);
BENCHMARK(mrc_coro_handoff);
BENCHMARK(mrc_coro_direct_handoff);
BENCHMARK(mrc_op_scenario_1);
BENCHMARK(mrc_op_scenario_2);
