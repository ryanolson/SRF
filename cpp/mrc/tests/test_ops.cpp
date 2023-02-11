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
#include "mrc/ops/manager.hpp"
#include "mrc/ops/operation.hpp"
#include "mrc/ops/operator.hpp"
#include "mrc/ops/output.hpp"
#include "mrc/ops/scheduling_terms/always_ready.hpp"
#include "mrc/ops/scheduling_terms/count_scheduling_term.hpp"
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

class PlusOne : public Operation<int, int>
{
  public:
    coroutines::Task<> execute(concepts::input_stream_of<int> auto& input_stream,
                               concepts::output_stream_of<int> auto& output_stream)
    {
        for (; input_stream; co_await input_stream.next())
        {
            auto data = input_stream.data() + 1;
            co_await output_stream.emit(data);
        }
    }
};

class Counter : public Operation<Tick>
{
  public:
    coroutines::Task<> execute(concepts::input_stream_of<Tick> auto& input_stream)
    {
        for (; input_stream; co_await input_stream.next())
        {
            m_counter += 1;
            LOG(INFO) << m_counter;
        }
    }

    std::size_t m_counter{0};
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

    static_assert(core::concepts::eval_concept_fn<MRC_CONCEPT(std::is_arithmetic_v), float>::value);
    static_assert(core::concepts::eval_concept_fn<MRC_CONCEPT_OF(std::same_as), float, float>::value);

    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 0, MRC_CONCEPT(std::is_arithmetic_v)>);
    static_assert(core::concepts::tuple_element_like_concept<decltype(t), 1, MRC_CONCEPT(std::is_arithmetic_v)>);

    static_assert(core::concepts::tuple_of_concept<decltype(t), MRC_CONCEPT(std::is_arithmetic_v)>);
    static_assert(!core::concepts::tuple_of_concept<decltype(t), MRC_CONCEPT(std::is_integral_v)>);
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

    auto tp = std::make_shared<coroutines::ThreadPool>(coroutines::ThreadPool::Options{.thread_count = 1});
    coroutines::TaskContainer tasks(tp);
    tasks.start(sink(xfer));

    // operator mock
    auto op = [&]() -> coroutines::Task<> {
        PlusOne plus_one;
        Outputs<PlusOne> outputs;
        co_await on_next_data.init();
        auto input_stream  = cpo::make_input_stream(on_next_data, source.get_token());
        auto output_stream = std::make_tuple(OutputStream<int>(xfer));
        // auto output_streams = cpo::make_output_stream(outputs);
        // static_assert(core::concepts::tuple_of_concept_of<decltype(output_streams),
        //                                                   MRC_CONCEPT_OF(ops::concepts::output_stream_of),
        //                                                   int>);
        LOG(INFO) << "calling execute";
        auto arguments = std::tuple_cat(std::make_tuple(input_stream), output_stream);
        co_await std::apply(
            [&](auto&&... args) {
                return plus_one.execute(std::forward<decltype(args)>(args)...);
            },
            arguments);

        xfer->close();
        // co_await cpo::execute(plus_one, input_stream, output_streams);
        LOG(INFO) << "op task finished";
        co_return;
    };

    tasks.start(op());
    coroutines::sync_wait(tasks.garbage_collect_and_yield_until_empty());
    // coroutines::sync_wait(coroutines::when_all(sink(xfer), op()));
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

TEST_F(TestOpsNext, Manager)
{
    auto tp = std::make_shared<coroutines::ThreadPool>(coroutines::ThreadPool::Options{.thread_count = 1});

    Manager manager(tp);
}

TEST_F(TestOpsNext, BasicOperator)
{
    auto tp = std::make_shared<coroutines::ThreadPool>(coroutines::ThreadPool::Options{.thread_count = 1});

    Manager manager(tp);

    CountSchedulingTerm counter{10};

    auto count_op = std::make_shared<detail::OperatorImpl<Counter, CountSchedulingTerm>>(counter);

    auto& remote = manager.register_operator("test", count_op);

    remote.advance_state(RequestedState::Init);
    LOG(INFO) << "f::init";
    remote.advance_state(RequestedState::Complete);
}

}  // namespace mrc::ops
