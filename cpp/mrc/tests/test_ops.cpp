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
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/cpo/scheduling_term.hpp"
#include "mrc/ops/input.hpp"
#include "mrc/ops/operation.hpp"
#include "mrc/ops/operator.hpp"
#include "mrc/ops/output.hpp"

// #include "mrc/ops/scheduling_terms/channel_reader.hpp"

using namespace mrc::channel::v2;

namespace mrc::ops {

// Output can be connected to any generic channel - it will use the write_task path
struct ScaleByTwo : public Operation<int>, public Output<int>
{
    Task<> evaluate(Stream<int> stream)
    {
        for (auto data = co_await stream.begin(); data != stream.end(); co_await ++data)
        {
            emit(*data * 2);
        }
        co_return;
    }
};

// Output can only be connected to a Writable<ImmediateChannel<int>>, it will use the faster async_write path
struct SubtractOne : public Operation<int>, public Output<ImmediateChannel<int>>
{
    Task<> evaluate(Stream<int> inputs)
    {
        for (auto input = co_await inputs.begin(); input != inputs.end(); co_await ++input)
        {
            co_await async_write(*input - 1);
        }
    }
};

// Sink - No Outputs
struct Logger : public Operation<int>
{
    static std::suspend_never evaluate(AsyncGenerator<int> inputs)
    {
        LOG(INFO) << "logger value: " << value;
        return {};
    }
};

static_assert(concepts::operable<ScaleByTwo>);
static_assert(concepts::operable<SubtractOne>);
static_assert(concepts::operable<Logger>);
static_assert(!concepts::stateful_operable<ScaleByTwo>);
static_assert(!concepts::stateful_operable<SubtractOne>);

static_assert(channel::v2::concepts::has_data_type<Input<int>>);

// static_assert(concepts::schedulable<Input<int>>);
static_assert(concepts::schedulable<Input<ImmediateChannel<int>>>);

std::shared_ptr<channel::v2::ImmediateChannel<int>> int_channel;
std::shared_ptr<channel::v2::ImmediateChannel<std::string>> str_channel;

void foo()
{
    // Operator<Logger, ChannelReader<channel::v2::ImmediateChannel<int>>> o;
    Operator<SubtractOne, Input<int>> any;

    Input<ImmediateChannel<int>> t;
    Input<int> i;

    auto d1 = cpo::scheduling_term::evaluate(t);
    auto d2 = cpo::scheduling_term::evaluate(i);

    static_assert(coroutines::concepts::awaiter_of<decltype(d1), expected<int, channel::Status>>);
    static_assert(coroutines::concepts::awaitable_of<decltype(d2), expected<int, channel::Status>>);

    // any.input().connect(int_channel);
    // o.m_scheduling_term.connect(int_channel);
    // o.m_operation.connect_channel(WritableChannelHandle<int> writable_channel)

    Operator<SubtractOne, Input<ImmediateChannel<int>>> specific;
    // specific.input().connect(int_channel);
}

// auto scale_by_two = make_operation(ScaleByTwo{}, Range<int>{0, 100});
// auto subtract_one = make_operation(SubtractOne{}, SchedulingTerm{});

}  // namespace mrc::ops
