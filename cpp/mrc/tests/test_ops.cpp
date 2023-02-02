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

#include "mrc/channel/status.hpp"
#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/core/concepts/tuple.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/sync_wait.hpp"
#include "mrc/coroutines/task_container.hpp"
#include "mrc/coroutines/thread_pool.hpp"
#include "mrc/coroutines/when_all.hpp"
#include "mrc/ops/concepts/input_stream.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/cpo/evaluate.hpp"
#include "mrc/ops/cpo/outputs.hpp"
#include "mrc/ops/cpo/scheduling_term.hpp"
#include "mrc/ops/input.hpp"
#include "mrc/ops/operation.hpp"
#include "mrc/ops/output.hpp"
#include "mrc/ops/scheduling_terms/always_ready.hpp"
#include "mrc/ops/scheduling_terms/on_next_data.hpp"

#include <gtest/gtest.h>

#include <coroutine>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>

using namespace mrc;

class TestOpsNext : public ::testing::Test
{};

// #include "mrc/ops/scheduling_terms/channel_reader.hpp"

using namespace mrc::channel::v2;

namespace mrc::ops {

static_assert(concepts::input_stream<InputStream<int>>);
static_assert(concepts::output_stream<OutputStream<int>>);

class PlusOne : public next::Operation<PlusOne, int, int>
{
  public:
    coroutines::Task<> execute(concepts::input_stream_of<input_type> auto& input_stream,
                               concepts::output_stream_of<output_type> auto& output_stream)
    {
        for (; input_stream; co_await input_stream.next())
        {
            auto data = input_stream.data() + 1;
            co_await output_stream.emit(data);
        }
    }
};

template <typename... Args>
void foo(std::string pos, Args&&... args)
{}

TEST_F(TestOpsNext, Tuples)
{
    std::tuple<int, double> t{42, 3.14};

    static_assert(core::concepts::tuple_like<decltype(t)>);

    std::apply(foo<int, double>, std::tuple_cat(std::make_tuple("hi"), t));

    static_assert(core::concepts::tuple_element_same_as<decltype(t), 0, int>);
    static_assert(core::concepts::tuple_element_same_as<decltype(t), 1, double>);

    static_assert(core::concepts::eval_concept_fn<CONCEPT(std::is_arithmetic_v), float>::value);
    static_assert(core::concepts::eval_concept_fn<CONCEPT_OF(std::same_as), float, float>::value);

    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 0, CONCEPT(std::is_arithmetic_v)>);
    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 1, CONCEPT(std::is_arithmetic_v)>);

    static_assert(core::concepts::tuple_of_concept<decltype(t), CONCEPT(std::is_arithmetic_v)>);
    static_assert(!core::concepts::tuple_of_concept<decltype(t), CONCEPT(std::is_integral_v)>);
}

TEST_F(TestOpsNext, OperationNext)
{
    auto generator = []() -> coroutines::AsyncGenerator<int> {
        for (int i = 0; i < 5; i++)
        {
            co_yield i;
        }
        LOG(INFO) << "source generator complete";
    };
    std::stop_source source;

    OnNextData on_next_data(generator());

    auto xfer = std::make_shared<coroutines::SymmetricTransfer<int>>();
    auto sink = [](std::shared_ptr<coroutines::SymmetricTransfer<int>> xfer) -> coroutines::Task<> {
        co_await xfer->initialize();
        while (*xfer)
        {
            LOG(INFO) << *xfer->data();
            co_await xfer->async_read();
        }
        LOG(INFO) << "sink finished";
        co_return;
    };

    // auto tp = std::make_shared<coroutines::ThreadPool>(coroutines::ThreadPool::Options{.thread_count = 1});
    // coroutines::TaskContainer tasks(tp);
    // tasks.start(sink(xfer));

    // operator mock
    auto op = [&]() -> coroutines::Task<> {
        PlusOne plus_one;
        Outputs<PlusOne> outputs;
        co_await on_next_data.init();
        auto input_stream   = cpo::make_input_stream(on_next_data, source.get_token());
        auto output_streams = cpo::make_output_stream(outputs);
        static_assert(core::concepts::tuple_of_concept_of<decltype(output_streams),
                                                          CONCEPT_OF(ops::concepts::output_stream_of),
                                                          int>);
        // co_await cpo::execute(plus_one, input_stream, output_streams);
        LOG(INFO) << "op task finished";
        co_return;
    };

    // tasks.start(op());
    // coroutines::sync_wait(tasks.garbage_collect_and_yield_until_empty());
    coroutines::sync_wait(coroutines::when_all(sink(xfer), op()));
}

TEST_F(TestOpsNext, StopSource)
{
    std::stop_source source;
    EXPECT_FALSE(source.stop_requested());
    source.request_stop();
    EXPECT_TRUE(source.stop_requested());
    source = {};
    EXPECT_FALSE(source.stop_requested());
}

struct MostGenericSource : public Source<int>
{
    template <concepts::input_stream_of<Tick> InputStreamT, concepts::output_stream_of<output_type> OutputStreamT>
    coroutines::Task<> execute(InputStreamT& input_stream, OutputStreamT& output_stream)
    {
        for (; input_stream; co_await input_stream.next())
        {
            m_counter += 1;
            co_await output_stream.emit(m_counter);
        }
        co_return;
    }

    // Since the execute Task can exit early and be restared, if we want to hold state between invocations of execute,
    // then we must hold it as part of the Operator rather than part of the Task.
    int m_counter{0};
};

// todo(clang-tidy-15)
// using SpecializedIntSource = MostGenericSource<OutputStream<int>, AlwaysReady::InputStream>;  // NOLINT

// Source which takes an InputStream<Tick>
struct IntSource : public Source<int>  // shorthand notation for Operation<Tick, int>
{
    Task<> execute(InputStream<Tick>& input_stream, OutputStream<int>& output_stream) final
    {
        while (input_stream)
        {
            m_counter += 1;
            co_await output_stream.emit(m_counter);
            co_await input_stream.next();
        }
        co_return;
    }

    // Since the execute Task can exit early and be restared, if we want to hold state between invocations of execute,
    // then we must hold it as part of the Operator rather than part of the Task.
    int m_counter{0};
};

struct ScaleByTwo : public Operation<int, int>
{
    // Both InputStream and OutputStream are passed to the Task by reference, which is a strong signal
    // that ownership is external and in this case, specific to the Operator
    // InputStream is a wrapper around a stop_token and an AsyncGeneratorIterator<T>
    // this allows the Operator to issue a stop on the token for an early exit, which can be used to
    // modify or swap the operation or to signal the end of the generator/stream
    // OutputStream is also owend by the Operator
    // With both InputStream and OutputStream fully decoupled from the Operation, the Operator has full
    // ownership of the Edge/Channel lifecycles
    Task<> execute(InputStream<int>& input_stream, OutputStream<int>& output_stream) final
    {
        while (input_stream)
        {
            input_stream.data() *= 2;
            co_await output_stream.emit(input_stream.data());
            co_await input_stream.next();
        }
        co_return;
    }
};

struct IntSink : Sink<int>  // short-hand notation for Operator<int, void>
{
    Task<> execute(InputStream<int>& input_stream) final
    {
        while (input_stream)
        {
            // do somthing with input_stream.data();
        }
        co_return;
    }
};

// number of arguments for evaluate ==> 1, 2 or 3:
// - if Operation<T>, then we need an InputStream<T>
// - if Output<T> or Outputs<Ts...>, then we need an OutputStream<T> or OutputStreams<Ts...>
// - if

// struct SingleOuputStream : public Operation<int>, Output<std::string>
// {
//     static Task<> execute(InputStream<int> input_stream, OutputStream<std::string> output_stream)
//     {
//         for (auto input = co_await input_stream.begin(); input != input_stream.end(); co_await ++input)
//         {
//             co_await output_stream.async_write(std::to_string(*input * 2));
//             co_await output_stream.async_write(std::to_string(*input * 2));
//         }
//         co_return;
//     }
// };

void foo()
{
    // auto pipeline = Pipeline() | ScaleByTwo() | SingleOutputStream();
    // auto pipeline = Pipeline() | ScaleByTwo() | ImmediateChannel() | SingleOutputStream();
}

// struct MultiOuputStreams : public Operation<int>, Outputs<int, std::string>
// {
//     Task<> evaluate(InputStream<int> input_stream, OutputStreams<int, std::string> output_streams)
//     {
//         auto& out_int = output_streams.get_output<0>();
//         auto& out_str = output_streams.get_output<1>();

//         for (auto input = co_await input_stream.begin(); input != input_stream.end(); co_await ++input)
//         {
//             co_await out_int.write(*input);
//             co_await out_str.write(std::to_stream(*input));
//             co_await out_str.write(std::to_stream(*input) + "another");
//         }
//     }
// };

// // Output can only be connected to a Writable<ImmediateChannel<int>>, it will use the faster async_write path
// struct SubtractOne : public Operation<int>, public Output<ImmediateChannel<int>>
// {
//     Task<> evaluate(Stream<int> inputs)
//     {
//         for (auto input = co_await inputs.begin(); input != inputs.end(); co_await ++input)
//         {
//             co_await async_write(*input - 1);
//         }
//     }
// };

// // Sink - No Outputs
// struct Logger : public Operation<int>
// {
//     static std::suspend_never evaluate(AsyncGenerator<int> inputs)
//     {
//         LOG(INFO) << "logger value: " << value;
//         return {};
//     }
// };

// static_assert(ops::concepts::source<SpecializedIntSource>);
// static_assert(ops::concepts::source<IntSource>);
// static_assert(ops::concepts::operation<ScaleByTwo>);
// static_assert(ops::concepts::sink<IntSink>);

// static_assert(concepts::operable<IntSource>);
// static_assert(concepts::operable<ScaleByTwo>);
// static_assert(concepts::operable<IntSink>);

// auto int_source = launch_control.make_operator<IntSource, AlwaysReady>(
//     "int_source", {/* IntSource Options */}, {/* SchedulingTerm Options*/});

// static_assert(!concepts::stateful_operable<ScaleByTwo>);
// static_assert(!concepts::stateful_operable<SubtractOne>);

// static_assert(core::concepts::has_data_type<Input<int>>);

// // static_assert(concepts::schedulable<Input<int>>);
// static_assert(concepts::schedulable<Input<ImmediateChannel<int>>>);

// std::shared_ptr<channel::v2::ImmediateChannel<int>> int_channel;
// std::shared_ptr<channel::v2::ImmediateChannel<std::string>> str_channel;

// void foo()
// {
//     // Operator<Logger, ChannelReader<channel::v2::ImmediateChannel<int>>> o;
//     Operator<SubtractOne, Input<int>> any;

//     Input<ImmediateChannel<int>> t;
//     Input<int> i;

//     auto d1 = cpo::scheduling_term::evaluate(t);
//     auto d2 = cpo::scheduling_term::evaluate(i);

//     static_assert(coroutines::concepts::awaiter_of<decltype(d1), expected<int, channel::Status>>);
//     static_assert(coroutines::concepts::awaitable_of<decltype(d2), expected<int, channel::Status>>);

//     // any.input().connect(int_channel);
//     // o.m_scheduling_term.connect(int_channel);
//     // o.m_operation.connect_channel(WritableChannelHandle<int> writable_channel)

//     Operator<SubtractOne, Input<ImmediateChannel<int>>> specific;
//     // specific.input().connect(int_channel);
// }

// auto scale_by_two = make_operation(ScaleByTwo{}, Range<int>{0, 100});
// auto subtract_one = make_operation(SubtractOne{}, SchedulingTerm{});

}  // namespace mrc::ops
