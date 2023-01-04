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

#include "mrc/channel/v2/immediate_channel.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
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
    Task<> evaluate(int&& value)
    {
        co_await async_write(value * 2);
        co_return;
    }
};

// Output can only be connected to a Writable<ImmediateChannel<int>>, it will use the faster async_write path
struct SubtractOne : public Operation<int>, public Output<ImmediateChannel<int>>
{
    Task<> evaluate(int&& value)
    {
        co_await async_write(value - 1);
        co_return;
    }
};

// Sink - No Outputs
struct Logger : public Operation<int>
{
    static Task<> evaluate(int&& value)
    {
        LOG(INFO) << "logger value: " << value;
        co_return;
    }
};

static_assert(concepts::operable<ScaleByTwo>);
static_assert(concepts::operable<SubtractOne>);
static_assert(!concepts::stateful_operable<ScaleByTwo>);
static_assert(!concepts::stateful_operable<SubtractOne>);

// static_assert(concepts::schedulable<Input<int>>);
// static_assert(concepts::schedulable<Input<ImmediateChannel<int>>>);

// std::shared_ptr<channel::v2::ImmediateChannel<int>> int_channel;
// std::shared_ptr<channel::v2::ImmediateChannel<std::string>> str_channel;

// void foo()
// {
//     // Operator<Logger, ChannelReader<channel::v2::ImmediateChannel<int>>> o;
//     Operator<SubtractOne, Input<int>> any;
//     any.input().connect(int_channel);
//     // o.m_scheduling_term.connect(int_channel);
//     // o.m_operation.connect_channel(WritableChannelHandle<int> writable_channel)

//     Operator<SubtractOne, ChannelReader<channel::v2::ImmediateChannel<int>>> specific;
//     specific.input().connect(int_channel);
// }

// auto scale_by_two = make_operation(ScaleByTwo{}, Range<int>{0, 100});
// auto subtract_one = make_operation(SubtractOne{}, SchedulingTerm{});

}  // namespace mrc::ops
