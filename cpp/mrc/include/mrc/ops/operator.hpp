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

#pragma once

#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/connectable.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"

namespace mrc::ops {

// Runtime object which we will use to set on the promise of the root task
// TaskContainer which we will use start the tasks
// A State/Status object which we can use external to push the state forward
// and allow state update events to be propagated back
// a scheduling term
// either a channel(s) adaptor or a generator adaptor

template <concepts::operable OperationT, concepts::schedulable SchedulingT>
requires std::same_as<typename OperationT::input_type, typename SchedulingT::data_type>
class Operator : public OperationT
{
  public:
    typename SchedulingT::data_type& input()
    requires concepts::input_connectable<SchedulingT>
    {
        return m_scheduling_term;
    }

    coroutines::Task<> main(coroutines::AsyncGenerator<typename OperationT::input_type> stream)
    {
        // run the main task on one thread of the default scheduler
        // co_await runtime.default_scheduler().schedule();

        // set the runtime property of the root/main task
        // child tasks will inhert runtimes of the their parents
        // co_await set_runtime(runtime);

        // await the initialize signal
        // co_await State::Initialize;
        co_await OperationT::setup();
        // StateTransistion: Initializing -> Initialized

        // await the start event
        // StateTransistion: Initialized -> Started
        co_await OperationT::main(std::move(stream));


        co_await OperationT::teardown();
    }


  private:
    SchedulingT m_scheduling_term;
};

}  // namespace mrc::ops
