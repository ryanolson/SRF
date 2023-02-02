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
    Finalize
};

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

    auto wait_until(RequestedState requested_state) -> AwaitStateOperation
    {
        return AwaitStateOperation{*this, requested_state};
    }

    std::stop_token get_stop_token() const
    {
        std::lock_guard lock(m_mutex);
        return m_stop_source.get_token();
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
        case RequestedState::Init:
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

    void issue_stop()
    {
        m_stop_source.request_stop();
        m_stop_source = {};
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

template <typename T, typename = typename T::output_type>
struct Outputs;

}

template <typename DataT>
struct Output
{
  public:
    using data_type = DataT;

    Output() : m_shared_state(std::make_shared<coroutines::SymmetricTransfer<DataT>>()), m_output_stream(m_shared_state)
    {}

    OutputStream<DataT>& output_stream()
    {
        return m_output_stream;
    }

    bool is_connected() const
    {
        return !m_shared_state;
    }

  protected:
    // the returned generator must be passed to an edge so it can be transfered to the downstream scheduling term
    // it is the responsiblity of another operator to execute the generator
    coroutines::AsyncGenerator<DataT> make_direct_generator()
    {
        auto shared_state = std::move(m_shared_state);
        CHECK(shared_state);

        auto generator = [](decltype(shared_state) shared_state) -> coroutines::AsyncGenerator<DataT> {
            co_await shared_state->initialize();
            while (*shared_state)
            {
                co_yield *(shared_state->data());
                co_await shared_state->async_read();
            }
        };

        return generator(std::move(shared_state));
    }

    // the returned writer should be owned and executed by the current operator
    template <channel::v2::concepts::writable ChannelT>
    coroutines::Task<> make_channel_writer(std::shared_ptr<ChannelT> channel)
    {
        auto shared_state = std::move(m_shared_state);
        CHECK(shared_state);

        auto writer = [](decltype(shared_state) shared_state, std::shared_ptr<ChannelT> channel) -> coroutines::Task<> {
            co_await shared_state->initialize();
            while (*shared_state)
            {
                channel::v2::async_write(*channel, (DataT &&) shared_state->data());
                co_await shared_state->async_read();
            }
        };

        return writer(shared_state, channel);
    }

  private:
    std::shared_ptr<coroutines::SymmetricTransfer<DataT>> m_shared_state;
    OutputStream<DataT> m_output_stream;

    template <typename OperationT>
    friend class detail::Outputs;
};

namespace detail {

// sinks have no outputs
template <concepts::has_output_type_of<void> OperationT>
class Outputs<OperationT>
{
  public:
    using data_type = void;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return 0;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs) -> std::tuple<>
    {
        return std::make_tuple();
    }
};

// operators that have a single output and are not parallel operators
// can be connected via channel edges or passthru edges
// direct generators require both single output and not parallel
template <concepts::has_single_output_type OperationT>
class Outputs<OperationT>
{
  public:
    using data_type = typename OperationT::output_type;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return 1;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs)
        -> std::tuple<OutputStream<data_type>>
    {
        return std::make_tuple(outputs.m_output.output_stream());
    }

    Output<data_type> m_output;
};

// operators with multiple outputs can only be connected by channel edges
// mandatory - each output must be attached to a channel edge
template <concepts::has_multi_output_type OperationT, typename... Types>  // NOLINT
class Outputs<OperationT, std::tuple<Types...>>
{
  public:
    using data_type = std::tuple<Types...>;

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return sizeof...(Types);
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_stream> _, Outputs& outputs)
        -> std::tuple<OutputStream<Types>...>
    {
        return std::apply([&](auto&&... args) { return std::make_tuple(args.output_stream()...); }, outputs.m_outputs);
    }

    std::tuple<Output<Types>...> m_outputs;
};

}  // namespace detail
template <typename T>
using Outputs = detail::Outputs<T>;  // NOLINT

namespace detail {

// put all private implementation details here
// use the public operation for the connectivity methods
template <concepts::operable OperationT, concepts::scheduling_term SchedulingT>
class OperatorImpl : public IOperator, public Outputs<OperationT>
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
    coroutines::Task<> run(/* std::shared_ptr<OperatorImpl> operator, */ std::shared_ptr<Controller> controller) final
    {
        co_await controller->wait_until(RequestedState::Init);
        co_await m_scheduling_term.init();
        co_await m_operation.init();
        // co_await m_outputs.init()

        auto stop_token = controller->get_stop_token();
        co_await controller->wait_until(RequestedState::Start);
        auto input_stream  = cpo::make_input_stream(m_scheduling_term, stop_token);
        auto output_stream = cpo::make_output_stream(*this);  // a sink should return an empty tuple
        auto arguments     = std::tuple_cat(std::make_tuple(input_stream), output_stream);
        co_await std::apply(m_operation->execute, arguments);
        // create input_stream
        // co_await m_operation->execute(input_stream);

        // if the execution was paused, then we will re-evaluate this while loop; otherwise, we enter the shutdown
        // phase

        // an operator that is used as a down stream generator will not run the above loop; however, it will advance the
        // controllers state to Join when it's upstream scheduling term is finished producing data
        co_await controller->wait_until(RequestedState::Join);
        // mark as joined, we might not immediate finalize so the running task are all allowed to finishe before we
        // start the tear down process

        co_await controller->wait_until(RequestedState::Finalize);
        // co_await m_operation->finalize();

        co_return;
    }

    OperationT m_operation;
    SchedulingT m_scheduling_term;
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

// OperationManager
// -

//
// Operator - need to survive
//  * Inputs
//  * Outputs
//  - Operation
//  - SchedulingTerm

// template <typename OperationT, typename SchedulingT>
// class Operator : public std::enable_shared_from_this<Operator<OperationT, SchedulingT>>
// {
//   public:
//     // Operator(std::shared_ptr<Controller> controller, OperationT&& operation = {}, SchedulingT&& scheduling_term =
//     {})
//     // :
//     //   m_operation(std::move(operation)),
//     //   m_scheduling_term(std::move(scheduling_term)),
//     //   m_controller(std::move(controller))
//     // {
//     //     CHECK(m_controller);
//     // }

//     ~Operator()
//     {
//         // if the state of the operator has advanced past RequestedState::None, then
//         // the execute task is running as part of the primary TaskContainer, which
//         if (m_controller->current_state() > RequestedState::None) {}
//     }

//   private:
//     coroutines::Task<> primary()
//     {
//         co_await m_controller->wait_until(RequestedState::Init);
//         // co_await m_operation->initialize();
//         // co_await m_controller->set_state(Controller::State::Initialized);

//         // not all operators are runnable
//         // if the operation is being directly called by the downstream operator, we don't execute
//         // the Operation::execute method, instead, we wrap the execute method in an async generator and use it
//         directly
//         // as a scheduling term.
//         // even if the operator is not runnable, we still need to initialize its state
//         // we implement this as a loop since we can pause execution using the per-start stop token. if the operator
//         is
//         // paused, then the Operation's execute task completes and the is_runnable() method returns true
//         while (is_runnable())
//         {
//             co_await m_controller->wait_until(RequestedState::Start);
//             if (is_runnable())
//             {
//                 auto input_stream = cpo::make_input_stream(m_scheduling_term);
//                 // create input_stream
//                 // co_await m_operation->execute(input_stream);
//             }

//             // if the execution was paused, then we will re-evaluate this while loop; otherwise, we enter the
//             shutdown
//             // phase
//         }

//         // an operator that is used as a down stream generator will not run the above loop; however, it will advance
//         the
//         // controllers state to Join when it's upstream scheduling term is finished producing data
//         co_await m_controller->wait_until(RequestedState::Join);
//         // mark as joined, we might not immediate finalize so the running task are all allowed to finishe before we
//         // start the tear down process

//         co_await m_controller->wait_until(RequestedState::Finalize);
//         // co_await m_operation->finalize();

//         co_return;
//     }

//     bool is_runnable() const;

//     OperationT m_operation;
//     SchedulingT m_scheduling_term;
//     std::shared_ptr<Controller> m_controller;
// };

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
//         // co_await wait_until(RequestedState::Init);
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
