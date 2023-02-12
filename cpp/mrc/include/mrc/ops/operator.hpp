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
class OperatorImpl : public IOperator
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
        co_await controller->wait_until(RequestedState::Init);
        co_await m_operation.init();
        controller->set_achieved_state(AchievedState::Initialized);

        co_await controller->wait_until(RequestedState::Start);
        co_await m_scheduling_term.init();

        auto output_streams = co_await m_outputs.init();
        std::vector<coroutines::Task<>> tasks;
        // auto tasks = m_outputs.make_writer_tasks();

        auto loop = [&]() -> coroutines::Task<> {
            for (auto input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token());
                 input_stream;
                 input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token()))
            {
                auto arguments = std::tuple_cat(std::make_tuple(input_stream), output_streams);
                controller->set_achieved_state(AchievedState::Running);

                co_await std::apply(
                    [&](auto&&... args) {
                        return m_operation.execute(std::forward<decltype(args)>(args)...);
                    },
                    arguments);

                // we need to await a restart or a stop/kill/join/complete first, then mark the achieved state to
                // stopped; this must be done in order and in separate task since the first will block

                auto wait_until = [](Controller& controller) -> coroutines::Task<> {
                    co_await controller.wait_until(RequestedState::Start);
                };

                auto set_achieved = [](Controller& controller) -> coroutines::Task<> {
                    controller.set_achieved_state(AchievedState::Stopped);
                    co_return;
                };

                co_await coroutines::when_all(wait_until(*controller), set_achieved(*controller));
            }

            // co_await m_scheduling_term.finalize();
            // co_await m_outputs.finalize();

            co_return;
        };

        // start all writer tasks first; then start the run loop
        tasks.push_back(loop());
        co_await coroutines::when_all(std::move(tasks));

        co_await controller->wait_until(RequestedState::Join);
        controller->set_achieved_state(AchievedState::Joined);

        co_await controller->wait_until(RequestedState::Complete);
        co_await m_operation.complete();
        controller->set_achieved_state(AchievedState::Completed);
    }

    OperationT m_operation;
    SchedulingT m_scheduling_term;
    Outputs<OperationT> m_outputs;
};

}  // namespace detail

template <concepts::source OperationT, concepts::scheduling_term SchedulingT>
class Operator<OperationT, SchedulingT> : public IOperator, public Outputs<OperationT>
{
  public:
  private:
    coroutines::Task<> main() final
    {
        co_return;
    }

    OperationT m_operation;
    SchedulingT m_scheduling_term;
};

}  // namespace mrc::ops
