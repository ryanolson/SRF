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
    class AwaitStateOperation
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
    };

    [[nodiscard]] AwaitStateOperation wait_until(StateT requested_state) noexcept
    {
        CHECK(state_is_awaitable(requested_state));
        return {*this, requested_state};
    }

  protected:
    explicit AsyncController(std::mutex& mutex) : m_mutex(mutex) {}

    virtual bool state_is_awaitable(const StateT& requested_state) const noexcept = 0;

    void set_state(StateT state, std::unique_lock<std::mutex> lock)
    {
        CHECK(lock.owns_lock());
        m_state       = state;
        auto* current = m_awaiters;
        while (current != nullptr)
        {
            if (m_state >= current->m_requested_state)
            {
                AwaitStateOperation* resume = current;

                // update root if curerent is root
                if (m_awaiters == current)
                {
                    m_awaiters = current->m_next;
                }
                current = current->m_next;

                // lock.unlock();
                // reschedule or resume resume->m_awaiting_coroutine
                // lock.lock();
            }
        }
    }

    std::mutex& get_mutex() noexcept
    {
        return m_mutex;
    }

  private:
    StateT m_state;
    AwaitStateOperation* m_awaiters{nullptr};
    std::mutex& m_mutex;

    friend AwaitStateOperation;
};

}  // namespace detail

enum class RequestedState : unsigned
{
    None,
    Init,
    Pause,
    Start,
    Stop,  // not awaitable
    Kill,  // not awaitable
    Join,
    Complete
};

enum class AchievedState : unsigned
{
    None,
    Initialized,
    Running,
    Stopped,
    Joined,
    Completed
};

struct RemoteController
{
    using AwaitAchievedOperation = typename detail::AsyncController<AchievedState>::AwaitStateOperation;

    virtual ~RemoteController() = default;

    // virtual const VertexInfo& vertex_info() const noexcept     = 0;
    virtual void advance_state(RequestedState requested_state) = 0;

    // virtual AwaitAchievedOperation wait_until(AchievedState state) = 0;
};

class Controller : public RemoteController
{
    class Requested : public detail::AsyncController<RequestedState>
    {
      public:
        using AsyncController<RequestedState>::AsyncController;
    };

    class Achieved : public detail::AsyncController<AchievedState>
    {
      public:
        using AsyncController<AchievedState>::AsyncController;
    };

  public:
    Controller(coroutines::Scheduler& scheduler, bool stoppable) : m_scheduler(scheduler), m_stoppable(stoppable) {}

    class AwaitStateOperation
    {
      public:
        bool await_ready() noexcept
        {
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

        auto await_resume() noexcept -> bool
        {
            // rescope the lock so it releases at the end of the current scope
            auto lock = std::move(m_lock);
            return true;
        }

      private:
        // the lock is acquired on construction from the parents mutex
        AwaitStateOperation(Controller& parent, RequestedState requested_state) :
          m_parent(parent),
          m_lock(m_parent.m_mutex),
          m_requested_state(requested_state)
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

    auto wait_until(RequestedState requested_state) noexcept -> AwaitStateOperation
    {
        return AwaitStateOperation{*this, requested_state};
    }

    std::stop_token get_stop_token() const
    {
        std::lock_guard lock(m_mutex);
        return m_stop_source.get_token();
    }

    void set_operator_state(AchievedState state) {}

  private:
    // needs resources/runtime object to get the default scheduler
    // needs some information from the operator to define the vertex info
    // needs the connectivity from the edges to traverse to other vertiex info objects

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
            m_awaiters                  = resume->m_next;

            // validate the
            DCHECK(resume->m_requested_state <= m_current_state);

            lock.unlock();
            m_scheduler.resume(resume->m_awaiting_coroutine);
            lock.lock();
        }
    }

    // change to request_pause
    void issue_pause()
    {
        std::lock_guard lock(m_mutex);
        m_current_state = RequestedState::Pause;
        m_stop_source.request_stop();
        m_stop_source = {};  // resets the
    }

    // change to request_stop
    void issue_stop()
    {
        if (m_stoppable)
        {
            std::lock_guard lock(m_mutex);
            m_current_state = RequestedState::Stop;
            m_stop_source.request_stop();
        }
    }

    // change to request_kill
    void request_kill()
    {
        std::lock_guard lock(m_mutex);
        m_current_state = RequestedState::Kill;
        m_stop_source.request_stop();
    }

    coroutines::Scheduler& m_scheduler;
    const bool m_stoppable;
    RequestedState m_current_state{RequestedState::None};
    AwaitStateOperation* m_awaiters{nullptr};
    std::stop_source m_stop_source;
    mutable std::mutex m_mutex;
};

}  // namespace mrc::ops
