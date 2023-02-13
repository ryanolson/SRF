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

#include "mrc/coroutines/scheduler.hpp"
#include "mrc/coroutines/task.hpp"

#include <mutex>
#include <stop_token>

namespace mrc::ops {

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

namespace detail {

template <typename StateT>
class AsyncController
{
  public:
    class AwaitStateOperation final
    {
      public:
        bool await_ready() const noexcept
        {
            return !(m_parent.m_state < m_requested_state);
        }

        void await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            // rescope the lock so it releases at the end of the current scope
            auto lock            = std::move(m_lock);
            m_awaiting_coroutine = awaiting_coroutine;
            m_next               = m_parent.m_awaiters;
            m_parent.m_awaiters  = this;
        }

        auto await_resume() noexcept -> bool
        {
            // rescope the lock so it releases at the end of the current scope
            auto lock = std::move(m_lock);
            return m_parent.m_state >= m_requested_state;
        }

      private:
        // the lock is acquired on construction from the parents mutex
        AwaitStateOperation(AsyncController& parent, StateT requested_state) :
          m_parent(parent),
          m_lock(m_parent.m_mutex),
          m_requested_state(requested_state)
        {}

        AsyncController& m_parent;
        std::unique_lock<std::mutex> m_lock;
        StateT m_requested_state;
        std::coroutine_handle<> m_awaiting_coroutine;
        AwaitStateOperation* m_next{nullptr};

        friend AsyncController;
    };

    explicit AsyncController(coroutines::Scheduler& scheduler, std::mutex& mutex, StateT state) :
      m_scheduler(scheduler),
      m_mutex(mutex),
      m_state(state)
    {}

    [[nodiscard]] AwaitStateOperation wait_until(StateT requested_state) noexcept
    {
        CHECK(state_is_awaitable(requested_state));
        return {*this, requested_state};
    }

    virtual bool state_is_awaitable(const StateT& requested_state) const noexcept = 0;

    void set_state(StateT state, std::unique_lock<std::mutex>& lock)
    {
        CHECK(lock.owns_lock());
        m_state       = state;
        auto* current = m_awaiters;
        while (current != nullptr)
        {
            if (m_state >= current->m_requested_state)
            {
                auto* resume = current;

                // update root nodeif curerent is root
                if (m_awaiters == current)
                {
                    m_awaiters = current->m_next;
                }
                current = current->m_next;

                m_scheduler.resume(resume->m_awaiting_coroutine);
            }
        }
    }

  private:
    coroutines::Scheduler& m_scheduler;
    StateT m_state;
    AwaitStateOperation* m_awaiters{nullptr};
    std::mutex& m_mutex;

    friend AwaitStateOperation;
};

}  // namespace detail

enum class RequestedState : unsigned
{
    None,
    Initialize,
    Pause,  // not awaitable
    Start,
    Complete,
    ExecuteToCompletion,
    Stop,  // not awaitable; stop gracefully
    Kill,  // not awaitable; kill immediately
    Finalize,
};

enum class AchievedState : unsigned
{
    None,
    Initialized,
    Running,
    Stopped,
    Completed,
    Finalized
};

struct RemoteController
{
    using AwaitAchievedState = typename detail::AsyncController<AchievedState>::AwaitStateOperation;  // NOLINT

    virtual ~RemoteController() = default;

    // virtual const VertexInfo& vertex_info() const noexcept     = 0;
    virtual void advance_state(RequestedState requested_state) = 0;

    virtual AwaitAchievedState wait_until(AchievedState state) noexcept = 0;
};

class Controller : public RemoteController
{
    class Requested final : public detail::AsyncController<RequestedState>
    {
      public:
        using detail::AsyncController<RequestedState>::AsyncController;

      private:
        bool state_is_awaitable(const RequestedState& state) const noexcept final
        {
            switch (state)
            {
            case RequestedState::Pause:
            case RequestedState::Stop:
            case RequestedState::Kill:
                return false;
            default:
                return true;
            }
            return true;
        }
    };

    class Achieved final : public detail::AsyncController<AchievedState>
    {
      public:
        using detail::AsyncController<AchievedState>::AsyncController;

      private:
        constexpr bool state_is_awaitable(const AchievedState& state) const noexcept final
        {
            return true;
        }
    };

  public:
    Controller(coroutines::Scheduler& scheduler, bool stoppable) :
      m_scheduler(scheduler),
      m_stoppable(stoppable),
      m_requested(scheduler, m_mutex, RequestedState::None),
      m_achieved(scheduler, m_mutex, AchievedState::None)
    {}

    [[nodiscard]] auto wait_until(RequestedState requested_state) noexcept -> Requested::AwaitStateOperation
    {
        return m_requested.wait_until(requested_state);
    }

    std::stop_token get_stop_token() const
    {
        std::lock_guard lock(m_mutex);
        return m_stop_source.get_token();
    }

    void set_achieved_state(AchievedState state)
    {
        std::unique_lock lock(m_mutex);
        m_achieved.set_state(state, lock);
    }

  private:
    // needs resources/runtime object to get the default scheduler
    // needs some information from the operator to define the vertex info
    // needs the connectivity from the edges to traverse to other vertiex info objects

    [[nodiscard]] auto wait_until(AchievedState requested_state) noexcept -> Achieved::AwaitStateOperation final
    {
        return m_achieved.wait_until(requested_state);
    }

    void advance_state(RequestedState requested_state) final
    {
        std::unique_lock lock(m_mutex);

        switch (requested_state)
        {
        case RequestedState::Initialize:
        case RequestedState::Start:
        case RequestedState::Complete:
        case RequestedState::Finalize:
            forward_state(requested_state, lock);
            break;

        case RequestedState::Pause:
            request_pause(lock);
            break;

        // Stop and Kill are special Actions/States
        case RequestedState::Stop:
            request_stop(lock);
            break;

        case RequestedState::Kill:
            request_kill(lock);
            break;

        default:
            LOG(FATAL) << "unhandled state change";
        }
    }

    // this function only advances the current state forward
    void forward_state(const RequestedState& requested_state, std::unique_lock<std::mutex>& lock)
    {
        m_requested.set_state(requested_state, lock);
    }

    // change to request_pause
    void request_pause(std::unique_lock<std::mutex>& lock)
    {
        m_requested.set_state(RequestedState::Pause, lock);
        m_stop_source.request_stop();
        m_stop_source = {};  // resets the stop source
    }

    bool is_stoppable() const noexcept
    {
        return m_stoppable;
    }

    // change to request_stop
    void request_stop(std::unique_lock<std::mutex>& lock)
    {
        if (m_stoppable)
        {
            m_requested.set_state(RequestedState::Stop, lock);
            m_stop_source.request_stop();
        }
    }

    // change to request_kill
    void request_kill(std::unique_lock<std::mutex>& lock)
    {
        m_requested.set_state(RequestedState::Kill, lock);
        m_stop_source.request_stop();
    }

    coroutines::Scheduler& m_scheduler;
    const bool m_stoppable;
    mutable std::mutex m_mutex;
    std::stop_source m_stop_source;

    Requested m_requested;
    Achieved m_achieved;
};

}  // namespace mrc::ops
