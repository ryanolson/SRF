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
#include "mrc/coroutines/scheduler.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>

namespace mrc::ops {

// Runtime object which we will use to set on the promise of the root task
// TaskContainer which we will use start the tasks
// A State/Status object which we can use external to push the state forward
// and allow state update events to be propagated back
// a scheduling term
// either a channel(s) adaptor or a generator adaptor

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

*/

class ControllerState : public std::enable_shared_from_this<ControllerState>
{
  public:
    enum class State
    {
        Constructed,
        Initialized,
        Running,
        Stopped,
        Completed,
        Destroyed
    };

    enum class RequestedState
    {
        None,
        Initialize,
        Start,
        Stop,
        Kill,
        Join,
        Finalize
    };

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
            m_awaiting_coroutine = awaiting_coroutine;
            m_next               = m_parent.m_awaiters;
            m_parent.m_awaiters  = this;
        }

        void await_resume() const noexcept
        {
            DCHECK(m_requested_state <= m_parent.current_state());
        }

      private:
        AwaitStateOperation(ControllerState& parent, RequestedState requested_state) : m_parent(parent) {}

        ControllerState& m_parent;
        RequestedState m_requested_state;
        std::coroutine_handle<> m_awaiting_coroutine;
        AwaitStateOperation* m_next{nullptr};

        friend ControllerState;
    };

    const State& state() const
    {
        return m_state;
    }

    const RequestedState& current_state() const
    {
        return m_requested_state;
    }

    void advance_state(RequestedState requested_state)
    {
        switch (requested_state)
        {
        case RequestedState::Initialize:
        case RequestedState::Start:
        case RequestedState::Join:
        case RequestedState::Finalize:
            do_forward_state_change(requested_state);
            break;
        case RequestedState::Stop:
        case RequestedState::Kill:
            break;
        default:
            LOG(FATAL) << "unhandled Controller::RequestedState";
        }
    }

    auto wait_until_state(RequestedState requested_state) -> AwaitStateOperation
    {
        // if requested state is a forward only state
        // evalute that the requested state is less than the current state
        // if not, we can throw before with return an awaitable
        // otherwise return an awaitable
        // in which await_ready is true until the current state is less than the requested stated
        return AwaitStateOperation{*this, requested_state};
    }

  private:
    ControllerState(coroutines::Scheduler& scheduler) : m_scheduler(scheduler) {}

    void do_forward_state_change(const RequestedState& requested_state) {}

    coroutines::Scheduler& m_scheduler;
    State m_state{State::Constructed};
    RequestedState m_requested_state{RequestedState::None};
    AwaitStateOperation* m_awaiters{nullptr};
    mutable std::mutex m_mutex;
};

struct VertexInfo
{};

struct ControllerOperatorAPI
{
    virtual ~ControllerOperatorAPI() = default;
};

struct ControllerManagerAPI
{
    virtual ~ControllerManagerAPI() = default;

    virtual const VertexInfo& vertex_info() const noexcept = 0;
};

}  // namespace mrc::ops
