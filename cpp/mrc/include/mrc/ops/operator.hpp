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

#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/channel.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/scheduler.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/coroutines/when_all.hpp"
#include "mrc/ops/api.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/controller.hpp"
#include "mrc/ops/cpo/outputs.hpp"
#include "mrc/ops/cpo/scheduling_term.hpp"
#include "mrc/ops/edge.hpp"
#include "mrc/ops/forward.hpp"
#include "mrc/ops/output.hpp"
#include "mrc/ops/outputs.hpp"

#include <concepts>
#include <coroutine>
#include <stop_token>
#include <type_traits>

namespace mrc::ops {

template <typename OperationT, typename SchedulingT>
class Operator;

namespace detail {

// put all private implementation details here
// use the public operation for the connectivity methods
template <concepts::operable OperationT, concepts::scheduling_term SchedulingT>
class OperatorImpl : public IOperator, private Component
{
  public:
    OperatorImpl()
    requires std::is_default_constructible_v<OperationT> and std::is_default_constructible_v<SchedulingT>
    = default;

    OperatorImpl(SchedulingT scheduling_term)
    requires std::is_default_constructible_v<OperationT>
      : m_scheduling_term(std::forward<SchedulingT>(scheduling_term))
    {}

    OperatorImpl(OperationT operation)
    requires std::is_default_constructible_v<SchedulingT>
      : m_operation(std::forward<OperationT>(operation))
    {}

    OperatorImpl(OperationT&& operation, SchedulingT&& scheduling_term) :
      m_operation(std::forward<OperationT>(operation)),
      m_scheduling_term(std::forward<SchedulingT>(scheduling_term))
    {}

  private:
    coroutines::Task<> main(std::shared_ptr<Controller> controller) final
    {
        return run(controller);
    }

    // only sources or standalone operators stop on stop
    // the other operation types gracefully shutdown when their respective upstream operators are finished sending data
    // either by a channel closing or an async generator completing.
    constexpr bool is_stoppable() const noexcept final
    {
        return std::same_as<typename OperationT::input_type, Tick>;
    }

    // validate all terms: inputs, outputs, scheduling, operation before calling run
    // run should only be called as part of the main task
    // this task will be placed into an detatched task container, so we should pass a shared_ptr created from
    // shared_from_this() to this task as a positional argument to ensure that the Operator is maintained for the
    // entirety of the run task
    coroutines::Task<> run(/* std::shared_ptr<OperatorImpl> operator, */ std::shared_ptr<Controller> controller)
    {
        co_await controller->wait_until(RequestedState::Initialize);
        co_await initialize();
        auto output_streams = cpo::make_output_streams(m_outputs);
        // auto concurrent_tasks = m_outputs.make_writer_tasks();
        std::vector<coroutines::Task<>> concurrent_tasks;
        controller->set_achieved_state(AchievedState::Initialized);

        co_await controller->wait_until(RequestedState::Start);

        auto loop = [&]() -> coroutines::Task<> {
            // this is the run loop where the achieved state can alternate between Running and Stopped
            // the requested state must be Start to enter this loop, the requested state is allowed to back to Paused
            // which would allow this loop to be restarted with another Start. The requested state can also be advanced
            // to Complete, Stop, Kill or Finalize; advancing to Complete or Finalize will allow the Operator to run to
            // a natural completion. A Stop is a graceful stop which will only effect Operators who's is_stoppable
            // returns true. A Kill will immediate stop the run loop on the next iteration regardless of if the Operator
            // is_stoppable or not.

            for (auto input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token());
                 input_stream;
                 input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token()))
            {
                co_await start();
                controller->set_achieved_state(AchievedState::Running);

                // start operation task
                co_await std::apply(
                    [&](auto&&... output_streams) {
                        return m_operation.execute(input_stream,
                                                   std::forward<decltype(output_streams)>(output_streams)...);
                    },
                    output_streams);

                // operation task completed - this means the operation:
                // - completed successfully and to completion
                //   - requested state == Start; stop token not triggered nor no reset, input_stream false
                // - paused input_stream via the stop token
                //   - requested state == Pause; stop token triggered, but reset
                // - the operation was stopped or killed
                //   - requested state > Start; stop token triggered and not reset

                // we need to await a restart or a stop/kill/join/complete first, then mark the achieved state to
                // stopped; this must be done in order and in separate task since the first will suspend

                auto wait_until = [&]() -> coroutines::Task<> {
                    co_await controller->wait_until(RequestedState::Start);
                };

                auto set_achieved = [&]() -> coroutines::Task<> {
                    co_await stop();
                    controller->set_achieved_state(AchievedState::NotRunning);
                    co_return;
                };

                // this ensures that the stop method is completed before a possible restart
                co_await coroutines::when_all(wait_until(), set_achieved());
            }

            co_await controller->wait_until(RequestedState::Complete);
            co_await complete();
            controller->set_achieved_state(AchievedState::Completed);
        };

        // start all writer tasks first; then start the run loop
        concurrent_tasks.push_back(loop());
        co_await coroutines::when_all(std::move(concurrent_tasks));

        co_await controller->wait_until(RequestedState::Finalize);
        co_await finalize();
        controller->set_achieved_state(AchievedState::Finalized);
    }

    coroutines::Task<> initialize() final
    {
        co_await m_outputs.initialize();
        co_await m_scheduling_term.initialize();
        co_await m_operation.initialize();

        // initialize any attached components
    }

    coroutines::Task<> start() final
    {
        co_await m_outputs.start();
        co_await m_scheduling_term.start();
        co_await m_operation.start();

        // start any attached components
    }

    coroutines::Task<> stop() final
    {
        co_await m_scheduling_term.stop();
        co_await m_outputs.stop();
        co_await m_operation.stop();

        // stop any attached components
    }

    coroutines::Task<> complete() final
    {
        co_await m_scheduling_term.complete();
        co_await m_outputs.complete();
        co_await m_operation.complete();

        // complete any attached components
    }

    coroutines::Task<> finalize() final
    {
        co_await m_scheduling_term.finalize();
        co_await m_outputs.finalize();
        co_await m_operation.finalize();

        // complete any attached components
    }

    OperationT m_operation;
    SchedulingT m_scheduling_term;
    Outputs<OperationT> m_outputs;
};

}  // namespace detail

}  // namespace mrc::ops
