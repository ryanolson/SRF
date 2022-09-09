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

#include "srf/utils/macros.hpp"

#include <coro/event.hpp>
#include <coro/ring_buffer.hpp>
#include <coro/task_container.hpp>
#include <expected/expected.hpp>
#include <glog/logging.h>

namespace srf::internal::coroutines {

template <coro::concepts::executor ExecutorTypeT>
class TaskQueue
{
    template <typename ResultTypeT>
    class TaskResult final
    {
      public:
        using return_type_t = tl::expected<ResultTypeT, std::exception_ptr>;

        TaskResult()  = default;
        ~TaskResult() = default;

        DELETE_COPYABILITY(TaskResult)
        DELETE_MOVEABILITY(TaskResult);

        auto result() -> coro::task<return_type_t>
        {
            co_await m_event;
            co_return std::move(m_result);
        }

      private:
        coro::event m_event;
        return_type_t m_result;
        friend TaskQueue;
    };

  public:
    // options
    struct Options
    {};

    // constructor
    TaskQueue(std::shared_ptr<ExecutorTypeT> executor, const Options ops = Options{}) : m_executor(executor)
    {
        CHECK(m_executor);
    }
    ~TaskQueue() = default;

    auto run() -> coro::task<void>
    {
        co_await m_executor->schedule();
        coro::task_container tc{m_executor};

        while (true)
        {
            auto expected = co_await m_ring_buffer.consume();
            if (!expected)
            {
                break;
            }
            tc.start(std::move(*expected));
        }

        co_await tc.garbage_collect_and_yield_until_empty();
        co_return;
    }

    auto stop() -> coro::task<void>
    {
        if (!m_running.load(std::memory_order::acquire))
        {
            co_return;
        }

        m_running.exchange(false, std::memory_order::acquire);

        co_await m_ring_buffer.produce([this]() -> coro::task<void> {
            while (!m_ring_buffer.empty())
            {
                co_await m_executor->yield();
            }
            m_ring_buffer.notify_waiters();
            co_return;
        }());

        co_return;
    }

    template <typename ResultTypeT>
    auto enqueue(coro::task<ResultTypeT>&& task) -> coro::task<std::shared_ptr<TaskResult<ResultTypeT>>>
    {
        using return_type_t = tl::expected<ResultTypeT, std::exception_ptr>;
        using state_type_t  = std::shared_ptr<TaskResult<ResultTypeT>>;

        if (!is_running())
        {
            LOG(FATAL) << "task queue stopped";
        }

        // run the task, capture the result and complete the event
        auto worker_task = [this](coro::task<ResultTypeT> task, state_type_t state) -> coro::task<void> {
            try
            {
                if constexpr (std::is_same_v<ResultTypeT, void>)
                {
                    co_await task;
                    state->m_result = {};
                }
                else
                {
                    state->m_result = co_await task;
                }
            } catch (...)
            {
                state->m_result = tl::make_unexpected(std::current_exception());
            }
            state->m_event.set();
            co_return;
        };

        // shared state with the coro::event to synchronize the worker and the future task
        auto state = std::make_shared<TaskResult<ResultTypeT>>();

        // start the task in the task contaienr
        co_await m_ring_buffer.produce(worker_task(std::move(task), state));

        // return the future_task to await on the result of the worker_task
        co_return state;
    }

    bool is_running() const
    {
        return m_running.load(std::memory_order::acquire);
    }

    ExecutorTypeT& executor() const
    {
        return *m_executor;
    }

  private:
    std::shared_ptr<ExecutorTypeT> m_executor;
    coro::ring_buffer<coro::task<void>, 128> m_ring_buffer;
    std::atomic<bool> m_running{true};
};
}  // namespace srf::internal::coroutines
