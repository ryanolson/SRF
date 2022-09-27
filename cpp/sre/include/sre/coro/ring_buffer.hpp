/**
 * SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "sre/coro/schedule_policy.hpp"
#include "sre/coro/thread_local_state.hpp"
#include "sre/coro/thread_pool.hpp"
#include "sre/system/thread.hpp"
#include "sre/thirdparty/expected.hpp"
#include "sre/trace/trace.hpp"

#include <atomic>
#include <coroutine>
#include <mutex>
#include <optional>
#include <vector>

namespace sre::coro {
/**
 * @tparam ElementT The type of element the ring buffer will store.  Note that this type should be
 *         cheap to move if possible as it is moved into and out of the buffer upon write and
 *         read operations.
 */
template <typename ElementT>
class RingBuffer
{
    struct DataHolder
    {
        ElementT element;
        trace::Handle<trace::Span> span;
    };

  public:
    struct Options
    {
        // capacity of ring buffer
        std::size_t capacity{8};

        // when there is an awaiting reader, the active execution context of the next writer will resume the awaiting
        // reader, the schedule_policy_t dictates how that is accomplished.
        SchedulePolicy reader_policy{SchedulePolicy::Reschedule};

        // when there is an awaiting writer, the active execution context of the next reader will resume the awaiting
        // writer, the producder_policy_t dictates how that is accomplished.
        SchedulePolicy writer_policy{SchedulePolicy::Reschedule};
    };

    enum class WriteResult
    {
        Success,
        Stopped
    };

    enum class ReadResult
    {
        Stopped
    };

    /**
     * @throws std::runtime_error If `num_elements` == 0.
     */
    explicit RingBuffer(Options opts = {}) :
      m_elements(opts.capacity),  // elements needs to be extended from just holding ElementT to include a TraceContext
      m_num_elements(opts.capacity),
      m_writer_policy(opts.writer_policy),
      m_reader_policy(opts.reader_policy)
    {
        if (m_num_elements == 0)
        {
            throw std::runtime_error{"num_elements cannot be zero"};
        }

        m_buffer_span = trace::get_tracer()->StartSpan("ring_buffer");
    }

    ~RingBuffer()
    {
        // Wake up anyone still using the ring buffer.
        notify_waiters();
    }

    RingBuffer(const RingBuffer<ElementT>&) = delete;
    RingBuffer(RingBuffer<ElementT>&&)      = delete;

    auto operator=(const RingBuffer<ElementT>&) noexcept -> RingBuffer<ElementT>& = delete;
    auto operator=(RingBuffer<ElementT>&&) noexcept -> RingBuffer<ElementT>&      = delete;

    struct WriteOperation : ThreadLocalState
    {
        WriteOperation(RingBuffer<ElementT>& rb, ElementT e) : m_rb(rb), m_policy(m_rb.m_writer_policy)
        {
            auto tracer = trace::get_tracer();
            // m_write_span = trace::get_tracer()->StartSpan("ring_buffer_write");
            m_e.span    = tracer->StartSpan("data", {.parent = m_rb.m_buffer_span->GetContext()});
            m_e.element = std::move(e);
        }

        auto await_ready() noexcept -> bool
        {
            // the lock is owned by the operation, not scoped to the await_ready function
            m_lock = std::unique_lock(m_rb.m_mutex);
            // m_write_span->AddEvent("start_on", {{"thead.id", sre::this_thread::get_id()}});
            return m_rb.try_write_locked(m_lock, m_e);
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // m_lock was acquired as part of await_ready; await_suspend is responsible for releasing the lock

            // Don't suspend if the stop signal has been set.
            if (m_rb.m_stopped.load(std::memory_order::acquire))
            {
                m_stopped = true;
                m_lock.unlock();
                return false;
            }

            // m_write_span->AddEvent("buffer_full");
            ThreadLocalState::suspend_coro_thread_local_state();

            m_awaiting_coroutine = awaiting_coroutine;
            m_thread_pool        = ThreadPool::from_current_thread();
            m_next               = m_rb.m_write_waiters;
            m_rb.m_write_waiters = this;
            m_lock.unlock();
            return true;
        }

        /**
         * @return write_result
         */
        auto await_resume() -> WriteResult
        {
            ThreadLocalState::resume_coro_thread_local_state();
            // m_write_span->End();
            return !m_stopped ? WriteResult::Success : WriteResult::Stopped;
        }

        WriteOperation& use_scheduling_policy(SchedulePolicy policy)
        {
            m_policy = policy;
            return *this;
        }

      private:
        friend RingBuffer;

        void resume()
        {
            // auto tracer = trace::get_tracer();
            // auto span   = tracer->StartSpan("resume suspended writer");
            // auto scope  = tracer->WithActiveSpan(span);

            if (m_thread_pool != nullptr && m_policy == SchedulePolicy::Reschedule)
            {
                // span->AddEvent("rescheduling on thread_pool", {{"thread_pool", m_thread_pool->description()}});
                // the default thread local context should be active at the start of any thread pool scheduling event
                // we don't have to worry about the resume
                m_thread_pool->resume(m_awaiting_coroutine);
            }
            else
            {
                // span->AddEvent("resume immediately");
                ThreadLocalState::suspend_coro_thread_local_state();
                m_awaiting_coroutine.resume();
                ThreadLocalState::resume_coro_thread_local_state();
            }

            // span->End();
        }

        /// The lock is acquired in await_ready; if ready it is release; otherwise, await_suspend should release it
        std::unique_lock<std::mutex> m_lock;
        /// The ring buffer the element is being written into.
        RingBuffer<ElementT>& m_rb;
        /// If the operation needs to suspend, the coroutine to resume when the element can be written.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// If the suspending coroutine was executing on a discoverable thread_pool, a pointer is captured
        ThreadPool* m_thread_pool{nullptr};
        /// Linked list of write operations that are awaiting to write their element.
        WriteOperation* m_next{nullptr};
        /// The element this write operation is producing into the ring buffer.
        DataHolder m_e;
        /// Was the operation stopped?
        bool m_stopped{false};
        /// Scheduling Policy - default provided by the RingBuffer, but can be overrided owner of the Awaiter
        SchedulePolicy m_policy;
        /// Span to measure the duration the writer spent writting data
        // trace::Handle<trace::Span> m_write_span{nullptr};
    };

    struct ReadOperation : ThreadLocalState
    {
        explicit ReadOperation(RingBuffer<ElementT>& rb) : m_rb(rb), m_policy(m_rb.m_reader_policy) {}

        auto await_ready() noexcept -> bool
        {
            // the lock is owned by the operation, not scoped to the await_ready function
            m_lock = std::unique_lock(m_rb.m_mutex);
            // m_read_span->AddEvent("start_on", {{"thead.id", sre::this_thread::get_id()}});
            return m_rb.try_read_locked(m_lock, this);
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
        {
            // m_lock was acquired as part of await_ready; await_suspend is responsible for releasing the lock

            // Don't suspend if the stop signal has been set.
            if (m_rb.m_stopped.load(std::memory_order::acquire))
            {
                m_stopped = true;
                m_lock.unlock();
                return false;
            }

            // m_read_span->AddEvent("buffer_empty");
            ThreadLocalState::suspend_coro_thread_local_state();

            m_awaiting_coroutine = awaiting_coroutine;
            m_thread_pool        = ThreadPool::from_current_thread();
            m_next               = m_rb.m_read_waiters;
            m_rb.m_read_waiters  = this;
            m_lock.unlock();
            return true;
        }

        /**
         * @return The consumed element or std::nullopt if the read has failed.
         */
        auto await_resume() -> tl::expected<ElementT, ReadResult>
        {
            // complete both the read and the data span
            // m_read_span->End();
            ThreadLocalState::resume_coro_thread_local_state();
            m_e.span->End();

            if (m_stopped)
            {
                return tl::make_unexpected(ReadResult::Stopped);
            }

            return std::move(m_e.element);
        }

        ReadOperation& use_scheduling_policy(SchedulePolicy policy)
        {
            m_policy = policy;
            return *this;
        }

      private:
        friend RingBuffer;

        void resume()
        {
            // auto tracer = trace::get_tracer();
            // auto span   = tracer->StartSpan("resume suspended reader");
            // auto scope  = tracer->WithActiveSpan(span);

            if (m_thread_pool != nullptr && m_policy == SchedulePolicy::Reschedule)
            {
                // span->AddEvent("rescheduling on thread_pool", {{"thread_pool", m_thread_pool->description()}});
                m_thread_pool->resume(m_awaiting_coroutine);
            }
            else
            {
                // span->AddEvent("resume immediately");
                ThreadLocalState::suspend_coro_thread_local_state();
                m_awaiting_coroutine.resume();
                ThreadLocalState::resume_coro_thread_local_state();
            }

            // span->End();
        }

        /// The lock is acquired in await_ready; if ready it is release; otherwise, await_suspend should release it
        std::unique_lock<std::mutex> m_lock;
        /// The ring buffer to read an element from.
        RingBuffer<ElementT>& m_rb;
        /// If the operation needs to suspend, the coroutine to resume when the element can be consumed.
        std::coroutine_handle<> m_awaiting_coroutine;
        /// If the suspending coroutine was executing on a discoverable thread_pool, a pointer is captured
        ThreadPool* m_thread_pool{nullptr};
        /// Linked list of read operations that are awaiting to read an element.
        ReadOperation* m_next{nullptr};
        /// The element this read operation will read.
        DataHolder m_e;
        /// Was the operation stopped?
        bool m_stopped{false};
        /// Scheduling Policy - default provided by the RingBuffer, but can be overrided owner of the Awaiter
        SchedulePolicy m_policy;
        /// Span measure time awaiting on reading data
        // trace::Handle<trace::Span> m_read_span;
    };

    /**
     * Produces the given element into the ring buffer.  This operation will suspend until a slot
     * in the ring buffer becomes available.
     * @param e The element to write.
     */
    [[nodiscard]] auto write(ElementT e) -> WriteOperation
    {
        return WriteOperation{*this, std::move(e)};
    }

    /**
     * Consumes an element from the ring buffer.  This operation will suspend until an element in
     * the ring buffer becomes available.
     */
    [[nodiscard]] auto read() -> ReadOperation
    {
        return ReadOperation{*this};
    }

    /**
     * @return The current number of elements contained in the ring buffer.
     */
    auto size() const -> size_t
    {
        std::atomic_thread_fence(std::memory_order::acquire);
        return m_used;
    }

    /**
     * @return True if the ring buffer contains zero elements.
     */
    auto empty() const -> bool
    {
        return size() == 0;
    }

    /**
     * Wakes up all currently awaiting writers and readers.  Their await_resume() function
     * will return an expected read result that the ring buffer has stopped.
     */
    auto notify_waiters() -> void
    {
        // Only wake up waiters once.
        if (m_stopped.load(std::memory_order::acquire))
        {
            return;
        }

        std::unique_lock lk{m_mutex};
        m_stopped.exchange(true, std::memory_order::release);

        while (m_write_waiters != nullptr)
        {
            auto* to_resume      = m_write_waiters;
            to_resume->m_stopped = true;
            m_write_waiters      = m_write_waiters->m_next;

            lk.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lk.lock();
        }

        while (m_read_waiters != nullptr)
        {
            auto* to_resume      = m_read_waiters;
            to_resume->m_stopped = true;
            m_read_waiters       = m_read_waiters->m_next;

            lk.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lk.lock();
        }
    }

  private:
    friend WriteOperation;
    friend ReadOperation;

    std::mutex m_mutex{};

    std::vector<DataHolder> m_elements;
    const std::size_t m_num_elements;
    const SchedulePolicy m_writer_policy;
    const SchedulePolicy m_reader_policy;
    trace::Handle<trace::Span> m_buffer_span;

    /// The current front pointer to an open slot if not full.
    size_t m_front{0};
    /// The current back pointer to the oldest item in the buffer if not empty.
    size_t m_back{0};
    /// The number of items in the ring buffer.
    size_t m_used{0};

    /// The LIFO list of write waiters - todo(ryan) - convert to FIFO for writers
    WriteOperation* m_write_waiters{nullptr};
    /// The LIFO list of read watier.
    ReadOperation* m_read_waiters{nullptr};

    std::atomic<bool> m_stopped{false};

    auto try_write_locked(std::unique_lock<std::mutex>& lk, DataHolder& e) -> bool
    {
        if (m_used == m_num_elements)
        {
            return false;
        }

        m_elements[m_front] = std::move(e);
        m_front             = (m_front + 1) % m_num_elements;
        ++m_used;

        ReadOperation* to_resume = nullptr;

        if (m_read_waiters != nullptr)
        {
            to_resume      = m_read_waiters;
            m_read_waiters = m_read_waiters->m_next;

            // Since the read operation suspended it needs to be provided an element to read.
            to_resume->m_e = std::move(m_elements[m_back]);
            m_back         = (m_back + 1) % m_num_elements;
            --m_used;  // And we just consumed up another item.
        }

        // release lock
        lk.unlock();

        if (to_resume != nullptr)
        {
            to_resume->resume();
        }

        return true;
    }

    auto try_read_locked(std::unique_lock<std::mutex>& lk, ReadOperation* op) -> bool
    {
        if (m_used == 0)
        {
            return false;
        }

        op->m_e = std::move(m_elements[m_back]);
        m_back  = (m_back + 1) % m_num_elements;
        --m_used;

        WriteOperation* to_resume = nullptr;

        if (m_write_waiters != nullptr)
        {
            to_resume       = m_write_waiters;
            m_write_waiters = m_write_waiters->m_next;

            // Since the write operation suspended it needs to be provided a slot to place its element.
            m_elements[m_front] = std::move(to_resume->m_e);
            m_front             = (m_front + 1) % m_num_elements;
            ++m_used;  // And we just written another item.
        }

        // release lock
        lk.unlock();

        if (to_resume != nullptr)
        {
            to_resume->resume();
        }

        return true;
    }
};

}  // namespace sre::coro
