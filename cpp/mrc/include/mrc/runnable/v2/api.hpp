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

#include "mrc/core/std23_expected.hpp"
#include "mrc/core/std26_tag_invoke.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/ring_buffer.hpp"

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace mrc::runnable::v2 {

namespace concepts {

using namespace coroutines::concepts;

template <typename T>
concept scheduling_type =
    requires(T t) {
        typename T::value_type;
        typename T::error_type;

        // explicit return_type
        requires std::same_as<typename T::return_type, std23::expected<typename T::value_type, typename T::error_type>>;

        // T must be an awaitable with the expected return_type
        requires awaitable<T>;
        requires awaitable_return_type_same_as<T, typename T::return_type>;
    };

// clang-format off

template <typename T>
concept async_operation_typed = requires(T t, typename T::value_type val) {
    typename T::value_type;
    { t.evaluate(std::move(val)) } -> awaiter;
};

template <typename T>
concept async_operation_void = requires(T t) {
    typename T::value_type;
    { t.evaluate() } -> awaiter;
};

template <typename T>
concept sync_operation_typed = requires(T t, typename T::value_type val) {
    typename T::value_type;
    { t.evaluate(std::move(val)) } -> std::same_as<void>;
};

template <typename T>
concept sync_operation_void = requires(T t) {
    typename T::value_type;
    { t.evaluate() } -> std::same_as<void>;
};


template <typename T>
concept operation_type = requires {
    requires async_operation_typed<T> || async_operation_void<T> ||
             sync_operation_typed<T>  || sync_operation_void<T>;
};
// clang-format on

}  // namespace concepts

namespace cpo {

inline constexpr struct scheduling_term_fn
{
    template <typename T>
    requires concepts::scheduling_type<std::remove_reference<std26::tag_invoke_result_t<scheduling_term_fn, const T&>>>
    constexpr auto operator()(const T& x)
    {
        return std26::tag_invoke(*this, x);
    }
} scheduling_term;

}  // namespace cpo

template <typename ValueT, typename ErrorT>
struct SchedulingTerm
{
    using value_type  = ValueT;
    using error_type  = ErrorT;
    using return_type = std23::expected<value_type, error_type>;
};

template <typename ValueT>
struct ComputeTerm
{
    using value_type = ValueT;
};

struct Runnable
{
    virtual ~Runnable() = default;

    virtual coroutines::Task<void> make_runner() = 0;
};

template <typename T>
class ImmediateChannel
{
  public:
    using mutex_type = std::mutex;

    struct WriteOperation
    {
        WriteOperation(ImmediateChannel& parent, T&& data) : m_parent(parent), m_data(std::move(data)) {}

        bool await_ready()
        {
            m_lock = std::unique_lock{m_parent.m_mutex};
            return m_parent.try_write_with_lock(m_data, m_lock);
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            DCHECK(m_lock.owns_lock());
            auto lock = std::move(m_lock);
            // m_lock was acquired as part of await_ready; await_suspend is responsible for releasing the lock

            // ThreadLocalContext::suspend_thread_local_context();

            m_awaiting_coroutine     = awaiting_coroutine;
            m_next                   = m_parent.m_write_waiters;
            m_parent.m_write_waiters = this;
            return true;
        }

        ImmediateChannel& m_parent;
        std::coroutine_handle<> m_awaiting_coroutine;
        WriteOperation* m_next{nullptr};
        T m_data;
        std::unique_lock<mutex_type> m_lock;
    };

    struct ReadOperation
    {
        bool await_ready()
        {
            m_lock = std::unique_lock(m_parent.m_mutex);
            return m_parent.try_read_with_lock(this, m_lock);
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            DCHECK(m_lock.owns_lock());
            auto lock = std::move(m_lock);

            m_awaiting_coroutine    = awaiting_coroutine;
            m_next                  = m_parent.m_read_waiters;
            m_parent.m_read_waiters = this;

            // nothing to resume
            if (m_parent.m_write_waiters == nullptr)
            {
                return std::noop_coroutine();
                ;
            }

            // resume the first writer which suspended without immediate processing
            auto to_resume           = m_parent.m_write_waiters;
            m_parent.m_write_waiters = to_resume->m_next;
            return to_resume.m_awaiting_coroutine;
        }

        ImmediateChannel& m_parent;
        std::coroutine_handle<> m_awaiting_coroutine;
        ReadOperation* m_next{nullptr};
        T m_data;
        std::unique_lock<mutex_type> m_lock;
    };

  private:
    bool try_write_with_lock(T& data, std::unique_lock<mutex_type>& lock)
    {
        if (m_read_waiters == nullptr)
        {
            // no awaiting readers, so we must suspend
            // lock must be released by await_suspend
            return false;
        }

        auto to_resume = m_read_waiters;
        m_read_waiters = m_read_waiters->m_next;

        lock.unlock();

        to_resume.m_data = std::move(data);
        to_resume.resume();
        return true;
    }

    bool try_read_with_lock(ReadOperation* read_op, std::unique_lock<mutex_type>& lock)
    {
        if (m_write_waiters == nullptr)
        {
            // no awaiting writers, so we must suspend
            // lock must be released by await_suspend
            return false;
        }

        auto resume_in_future    = m_write_waiters;
        m_write_waiters          = m_write_waiters->m_next;
        resume_in_future->m_next = nullptr;

        // resume_in_future is placed at the end of the m_write_waiters fifo queue
        if (m_write_resumers == nullptr)
        {
            m_write_resumers = resume_in_future;
        }
        else
        {
            auto last = m_write_resumers;
            while (last->m_next != nullptr)
            {
                last = last->m_next;
            }
            last->m_next = resume_in_future;
        }

        lock.unlock();

        read_op->m_data = std::move(resume_in_future.m_data);
        return true;
    }

    mutex_type m_mutex;
    WriteOperation* m_write_waiters{nullptr};
    WriteOperation* m_write_resumers{nullptr};
    ReadOperation* m_read_waiters{nullptr};
};

template <concepts::scheduling_type SchedulingT, concepts::operation_type OperationT>
requires std::same_as<typename SchedulingT::value_type, typename OperationT::value_type>
class Operator : public Runnable
{
  public:
  private:
    coroutines::Task<void> make_runner() final
    {
        // if (m_started.load(std::memory_order::acquire))
        // {
        //     throw return true;
        // }
        return [](SchedulingT&& scheduling_term, OperationT&& operation) -> coroutines::Task<void> {
            while (true)
            {
                auto data = co_await scheduling_term;

                if (!data)
                {
                    break;
                }

                if constexpr (concepts::async_operation_typed<OperationT>)
                {
                    co_await operation.evaluate(std::move(*data));
                }
                else if constexpr (concepts::async_operation_void<OperationT>)
                {
                    co_await operation.evalute();
                }
                else if constexpr (concepts::sync_operation_typed<OperationT>)
                {
                    operation.evalute(std::move(*data));
                }
                else if constexpr (concepts::sync_operation_void<OperationT>)
                {
                    operation.evalute();
                }
                else
                {
                    LOG(FATAL) << "should be unreachable";
                }
            }
            co_return;
        }(std::move(m_scheduling_term), std::move(m_operation));
    }

    SchedulingT m_scheduling_term;
    OperationT m_operation;
    std::atomic<bool> m_started{false};
};

}  // namespace mrc::runnable::v2
