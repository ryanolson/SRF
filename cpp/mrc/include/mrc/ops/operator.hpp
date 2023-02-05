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
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
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

// Runtime object which we will use to set on the promise of the root task
// TaskContainer which we will use start the tasks
// A State/Status object which we can use external to push the state forward
// and allow state update events to be propagated back
// a scheduling term
// either a channel(s) adaptor or a generator adaptor

struct IOperator
{
    virtual ~IOperator() = default;

    virtual coroutines::Task<> main() = 0;
};

/**

The Controller must be a shared pointer. A copy of the shared pointer is provided to the control plane client/operations
manager and a second copy is given to the Operation for which the Controller controls.

From the perspective of the Operator, the API of the Controller should:
- allow the Operation's primary/main task to yield until a specific state is requested (`wait_until(RequestedState)`).

From the perspective of the OperationsManager, the API of the Controller should:
- provide details about the Operator/Operation
  - name, parent, namespace, type, options
  - provide back traversable edges which should link other
- issue actions which will advance the state of the Operator/Operation
- get callbacks when the Operator confirms state change

// if concepts::source<OperatorT>, then SchedulingT must provide a concepts::input_stream_of<Tick>
// if concepts::operation<OperatorT>, i.e. not a Source or Sink, then the scheduling term must be either a Edge or
// a ConcurrentEdge depending on if operation<Operationt> is a parallel_operation<OperationT>

// we must form connection based on detail of the OperationT

// case: source
// - no upstream, but we need a type of scheduling term which will provide an input stream
// - has 1 or more downstream edges, which requires forming an edge to a downstream scheduling type

// case: operation
// - scheudling_term has at least 1 upstream edge, produced a single value
// -

*/

class Controller;

enum class RequestedState
{
    None,
    Init,
    Pause,
    Start,
    Stop,
    Kill,
    Join,
    Complete
};

enum class OperatorState
{
    Constructed,
    Initialized,
    Running,
    Stopped,
    Joined,
    Completed
};

struct RemoteController
{
    virtual ~RemoteController() = default;

    // virtual const VertexInfo& vertex_info() const noexcept     = 0;
    virtual void advance_state(RequestedState requested_state) = 0;
};

class Controller : public RemoteController, public std::enable_shared_from_this<Controller>
{
  public:
    class AwaitStateOperation
    {
      public:
        bool await_ready() noexcept
        {
            // if true, then resume immediately; else if false, then suspend
            // the statement in the () represents the truthy condition to suspend if the current state is less than the
            // requested state. this means that  the controller has not requested that the state should be advanced to
            // the level of the requester and there for the requester should yield until the controller advances.
            return !(m_parent.current_state() < m_requested_state);
        }

        void await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            // rescope the lock so it releases at the end of the current scope
            auto lock            = std::move(m_lock);
            m_awaiting_coroutine = awaiting_coroutine;
            m_next               = m_parent.m_awaiters;
            m_parent.m_awaiters  = this;
        }

        auto await_resume() noexcept -> RequestedState
        {
            // rescope the lock so it releases at the end of the current scope
            // if we did not suspend, we will still own the lock
            // if we are resuming after as suspend, this is a no-op
            auto lock = std::move(m_lock);
            return m_parent.current_state();
        }

      private:
        // the lock is acquired on construction from the parents mutex
        AwaitStateOperation(Controller& parent, RequestedState requested_state) :
          m_parent(parent),
          m_lock(m_parent.m_mutex)
        {}

        Controller& m_parent;
        std::unique_lock<std::mutex> m_lock;
        RequestedState m_requested_state;
        std::coroutine_handle<> m_awaiting_coroutine;
        AwaitStateOperation* m_next{nullptr};

        friend Controller;
    };

    const RequestedState& current_state() const
    {
        return m_current_state;
    }

    auto wait_until(RequestedState requested_state) -> AwaitStateOperation
    {
        return AwaitStateOperation{*this, requested_state};
    }

    std::stop_token get_stop_token() const
    {
        std::lock_guard lock(m_mutex);
        return m_stop_source.get_token();
    }

    void set_operator_state(OperatorState state) {}

  private:
    // needs resources/runtime object to get the default scheduler
    // needs some information from the operator to define the vertex info
    // needs the connectivity from the edges to traverse to other vertiex info objects
    Controller(coroutines::Scheduler& scheduler) : m_scheduler(scheduler) {}

    void advance_state(RequestedState requested_state) final
    {
        std::unique_lock lock(m_mutex);

        switch (requested_state)
        {
        case RequestedState::Init:
        case RequestedState::Start:
        case RequestedState::Join:
        case RequestedState::Complete:
            forward_state(requested_state, lock);
            break;

        case RequestedState::Pause:
            issue_pause();
            break;

        // Stop and Kill are special Actions/States
        case RequestedState::Stop:
        case RequestedState::Kill:
            break;
        default:
            LOG(FATAL) << "unhandled state change";
        }
    }

    // this function only advances the current state forward
    void forward_state(const RequestedState& requested_state, std::unique_lock<std::mutex>& lock)
    {
        DCHECK(m_current_state < requested_state);
        m_current_state = requested_state;

        while (m_awaiters != nullptr)
        {
            AwaitStateOperation* resume = m_awaiters;

            // validate the
            DCHECK(resume->m_requested_state <= m_current_state);

            m_awaiters = resume->m_next;

            lock.unlock();
            m_scheduler.resume(resume->m_awaiting_coroutine);
            lock.lock();
        }
    }

    void issue_pause()
    {
        std::lock_guard lock(m_mutex);
        m_current_state = RequestedState::Pause;
        m_stop_source.request_stop();
        m_stop_source = {};  // resets the
    }

    coroutines::Scheduler& m_scheduler;
    RequestedState m_current_state{RequestedState::None};
    AwaitStateOperation* m_awaiters{nullptr};
    std::stop_source m_stop_source;
    mutable std::mutex m_mutex;
};

template <typename OperationT, typename SchedulingT>
class Operator;

namespace detail {

// put all private implementation details here
// use the public operation for the connectivity methods
template <concepts::operable OperationT, concepts::scheduling_term SchedulingT>
class OperatorImpl : public IOperator
{
    coroutines::Task<> main() final
    {
        // do stuff

        auto controller = std::make_shared<Controller>();

        return run(controller);
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
        controller->set_operator_state(OperatorState::Initialized);

        co_await controller->wait_until(RequestedState::Start);
        co_await m_scheduling_term.init();

        auto output_streams = co_await m_outputs.init();

        for (auto input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token());
             co_await controller->wait_until(RequestedState::Start) && input_stream;
             input_stream = cpo::make_input_stream(m_scheduling_term, controller->get_stop_token()))
        {
            auto arguments = std::tuple_cat(std::make_tuple(input_stream), output_streams);
            controller->set_operator_state(OperatorState::Running);
            co_await std::apply(m_operation.execute, arguments);
            controller->set_operator_state(OperatorState::Stopped);
        }

        co_await m_scheduling_term.finalize();
        co_await m_outputs.finalize();

        co_await controller->wait_until(RequestedState::Join);
        controller->set_operator_state(OperatorState::Joined);

        co_await controller->wait_until(RequestedState::Complete);
        co_await m_operation.finalize();
        controller->set_operator_state(OperatorState::Completed);

        co_return;
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
