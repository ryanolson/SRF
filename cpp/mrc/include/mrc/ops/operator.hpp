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

*/

class Controller;

enum class RequestedState
{
    None,
    Initialize,
    Pause,
    Start,
    Stop,
    Kill,
    Join,
    Finalize
};

struct VertexInfo
{};

struct RemoteController
{
    virtual ~RemoteController() = default;

    virtual const VertexInfo& vertex_info() const noexcept     = 0;
    virtual void advance_state(RequestedState requested_state) = 0;
};

class Controller : public RemoteController, public std::enable_shared_from_this<Controller>
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

        auto await_resume() noexcept -> void
        {
            // rescope the lock so it releases at the end of the current scope
            // if we did not suspend, we will still own the lock
            // if we are resuming after as suspend, this is a no-op
            auto lock = std::move(m_lock);
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

    auto wait_until_state(RequestedState requested_state) -> AwaitStateOperation
    {
        return AwaitStateOperation{*this, requested_state};
    }

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
        case RequestedState::Initialize:
        case RequestedState::Pause:
        case RequestedState::Start:
        case RequestedState::Join:
        case RequestedState::Finalize:
            forward_state(requested_state, lock);
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

    coroutines::Scheduler& m_scheduler;
    RequestedState m_current_state{RequestedState::None};
    AwaitStateOperation* m_awaiters{nullptr};
    mutable std::mutex m_mutex;
};

// template <typename OperationT, typename SchedulingT>
// class Operator;

// template <concepts::source SourceT, typename SchedulingT>
// class Operator<SourceT, SchedulingT> : public IOperator
// {
//   public:
//     coroutines::Task<> execute() final
//     {
//         co_return;
//     }

//   private:
//     SourceT m_operation;
//     SchedulingT m_scheduling_term;
// };

class Operator
{
  public:
    coroutines::Task<> main()
    {
        co_await m_controller->wait_until_state(RequestedState::Initialize);
        // co_await m_operation->initialize();
        // co_await m_controller->set_state(Controller::State::Initialized);

        // not all operators are runnable
        // if the operation is being directly called by the downstream operator, we don't execute
        // the Operation::execute method, instead, we wrap the execute method in an async generator and use it directly
        // as a scheduling term.
        // even if the operator is not runnable, we still need to initialize its state
        // we implement this as a loop since we can pause execution using the per-start stop token. if the operator is
        // paused, then the Operation's execute task completes and the is_runnable() method returns true
        while (is_runnable())
        {
            co_await m_controller->wait_until_state(RequestedState::Start);
            if (is_runnable())
            {
                // create input_stream
                // co_await m_operation->execute(input_stream);
            }

            // if the execution was paused, then we will re-evaluate this while loop; otherwise, we enter the shutdown
            // phase
        }

        // an operator that is used as a down stream generator will not run the above loop; however, it will advance the
        // controllers state to Join when it's upstream scheduling term is finished producing data
        co_await m_controller->wait_until_state(RequestedState::Join);
        // mark as joined, we might not immediate finalize so the running task are all allowed to finishe before we
        // start the tear down process

        co_await m_controller->wait_until_state(RequestedState::Finalize);
        // co_await m_operation->finalize();

        co_return;
    }

  private:
    bool is_runnable() const;

    std::shared_ptr<Controller> m_controller;
};

// template <concepts::operable OperationT, concepts::schedulable SchedulingT>
// requires std::same_as<typename OperationT::input_type, typename SchedulingT::data_type>
// class Operator<OperationT, SchedulingT> : OperationT
// {
//   public:
//     // typename SchedulingT::data_type& input()
//     // requires concepts::input_connectable<SchedulingT>
//     // {
//     //     return m_scheduling_term;
//     // }

//     coroutines::Task<> main(coroutines::AsyncGenerator<typename OperationT::input_type>&& stream)
//     {
//         // run the main task on one thread of the default scheduler
//         // co_await runtime.default_scheduler().schedule();

//         // set the runtime property of the root/main task
//         // child tasks will inhert runtimes of the their parents
//         // co_await set_runtime(runtime);

//         // await the initialize signal
//         // co_await wait_until(RequestedState::Initialize);
//         // forward_state(State::Initializing);
//         // co_await OperationT::setup();
//         // forward_state(State::Initialized);

//         // await the start event
//         co_await wait_until(RequestedState::Start);
//         forward_state(State::Starting);
//         // if this had concurrency > 1, we would make an instance per child task
//         auto instance = m_scheduling_term.make_instance();
//         auto input_stream = instance.make_input_stream();
//         // create input streams and associate their stop tokens with the controller
//         // forward_state(State::Started);
//         // for(auto input_iterator = co_await input_stream.begin(); input_iterator != input_stream.end();;)
//         // {
//         //     co_await OperatorT::execute(stop_token, input_iterator, output_stream);
//         // }
//         // co_await OperationT::main(std::move(stream));
//         // co_await state.done();

//         // await shutdown
//         // co_await state.destroy();
//         // co_await OperationT::teardown();
//         // co_await state.destroyed();

//         // await join
//         // co_await OperationT::join();
//     }

//   private:
//     SchedulingT m_scheduling_term;
// };

}  // namespace mrc::ops
