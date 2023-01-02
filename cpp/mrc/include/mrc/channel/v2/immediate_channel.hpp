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

#include "mrc/channel/status.hpp"
#include "mrc/channel/v2/channel.hpp"
#include "mrc/channel/v2/cpo/close.hpp"
#include "mrc/channel/v2/cpo/read.hpp"
#include "mrc/channel/v2/cpo/write.hpp"
#include "mrc/core/error.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/coroutines/scheduler.hpp"

#include <glog/logging.h>
#include <unifex/tag_invoke.hpp>

#include <coroutine>
#include <mutex>
#include <utility>

namespace mrc::channel::v2 {

/**
 * @brief Channel which passes both data an the execution context (if possible) from the writer to the reader
 *
 * The j shall:
 *  - Writes shall suspend if there are no awaiting Readers
 *  - Reads shall suspend if there are no awaiting Writers (with or without data)
 *  - Awaiting writers holding data are always processed first
 *  - Suspended writers with data are put into a LIFO linked-list
 *  - Suspended writers without data (resumers) are put in a FIFO  linked-list to be resumed after all read operations
 *    are completed.
 *  - If no incoming data is available, writers are resumed in FIFO ordering from the resume queue
 *  - Back pressure is managed by transferring the execution context downstream, pausing upstream progress
 *  - Does not enable pipeline concurrency as the execution context is transferred.
 *
 * @tparam T
 */
template <typename T>
class Immediate : public ChannelBase<T>
{
    using mutex_type = std::mutex;

    // mrc: hotpath
    struct WriteOperation
    {
        WriteOperation(Immediate& parent, T&& data) : m_parent(parent), m_data(std::move(data)) {}

        // writes always suspend
        constexpr static auto await_ready() noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            auto lock            = std::unique_lock{m_parent.m_mutex};
            m_awaiting_coroutine = awaiting_coroutine;
            m_scheduler          = coroutines::Scheduler::from_current_thread();

            // if the channel was closed, resume immediate and throw an error in the await_resume method
            if (m_parent.m_closed.load(std::memory_order::acquire)) [[unlikely]]
            {
                m_channel_closed = true;
                return awaiting_coroutine;
            }

            // if there are no readers to resume, we insert ourself into the lifo queue of writers with data and yield
            if (m_parent.m_read_waiters == nullptr)
            {
                m_next                   = m_parent.m_write_waiters;
                m_parent.m_write_waiters = this;
                return std::noop_coroutine();
            }

            // otherwise we prepare the reader for resumption
            auto* reader            = m_parent.m_read_waiters;
            m_parent.m_read_waiters = reader->m_next;
            reader->m_data          = std::move(m_data);

            // then we insert ourself at the end of the fifo queue of writers without data awaiting to be resumed
            if (m_parent.m_write_resumers == nullptr)
            {
                m_parent.m_write_resumers = this;
            }
            else
            {
                // put current writer at the end of the fifo writer resumer list
                auto* write_resumer = m_parent.m_write_resumers;
                while (write_resumer->m_next != nullptr)
                {
                    write_resumer = write_resumer->m_next;
                }
                write_resumer->m_next = this;
            }

            // resume the reader via symmetric transfer
            return reader->m_awaiting_coroutine;
        }

        auto await_resume() -> void
        {
            if (m_channel_closed) [[unlikely]]
            {
                auto error = Error::create(ErrorCode::ChannelClosed, "write failed on closed channel");
                // LOG(ERROR) << error.value().message();
                throw error.value();
            }
            DVLOG(10) << "resuming writer";
        }

        Immediate& m_parent;
        std::coroutine_handle<> m_awaiting_coroutine;
        coroutines::Scheduler* m_scheduler{nullptr};
        WriteOperation* m_next{nullptr};
        T m_data;
        bool m_channel_closed{false};
        std::unique_lock<mutex_type> m_lock;
    };

    // mrc: hotpath
    struct ReadOperation
    {
        bool await_ready()
        {
            m_lock = std::unique_lock(m_parent.m_mutex);
            return m_parent.try_read_with_lock(this, m_lock);
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            DCHECK(m_lock.owns_lock());
            auto lock = std::move(m_lock);

            m_awaiting_coroutine    = awaiting_coroutine;
            m_next                  = m_parent.m_read_waiters;
            m_parent.m_read_waiters = this;

            if (m_resume != nullptr)
            {
                DVLOG(10) << "suspending read; resuming writer";
                return m_resume->m_awaiting_coroutine;
            }

            DVLOG(10) << "suspending read; thing to resume";
            return std::noop_coroutine();
        }

        auto await_resume() noexcept -> mrc::expected<T, Status>
        {
            if (m_channel_closed) [[unlikely]]
            {
                return mrc::unexpected(Status::closed);
            }

            return {std::move(m_data)};
        }

        Immediate& m_parent;
        std::coroutine_handle<> m_awaiting_coroutine;
        ReadOperation* m_next{nullptr};
        WriteOperation* m_resume{nullptr};
        T m_data;
        bool m_channel_closed{false};
        std::unique_lock<mutex_type> m_lock;
    };

    [[nodiscard]] WriteOperation async_write(T&& data)
    {
        // mrc: hotpath
        return WriteOperation{*this, std::move(data)};
    }

    [[nodiscard]] ReadOperation async_read()
    {
        // mrc: hotpath
        return ReadOperation{*this};
    }

    void close()
    {
        // Only wake up waiters once.
        if (m_closed.load(std::memory_order::acquire))
        {
            return;
        }

        std::unique_lock lock{m_mutex};
        auto first_closer = !m_closed.exchange(true, std::memory_order::release);

        // only the first caller of close should continue
        if (first_closer)
        {
            // the readers flush the writers, then after all writers are finished,
            // the readers will see the channel is closed and resume with the closed status
            while (m_read_waiters != nullptr)
            {
                auto* to_resume = m_read_waiters;
                m_read_waiters  = m_read_waiters->m_next;
                lock.unlock();
                to_resume->m_channel_closed = true;
                to_resume->m_awaiting_coroutine.resume();
                lock.lock();
            }
        }
    }

    // mrc: hotpath
    bool try_read_with_lock(ReadOperation* read_op, std::unique_lock<mutex_type>& lock)
    {
        // if there are any writers in any state, we will resume them
        while (m_write_waiters != nullptr || m_write_resumers)
        {
            // first process any writer that still holds data
            if (m_write_waiters != nullptr)
            {
                // pop writer off the lifo writers queue
                auto resume_in_future    = m_write_waiters;
                m_write_waiters          = m_write_waiters->m_next;
                resume_in_future->m_next = nullptr;

                // transfer the data object to this ReadOperation
                read_op->m_data = std::move(resume_in_future->m_data);

                // add resume_in_future to the fifo resumers queue
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
                return true;
            }

            // there were no writers with data, so there must be at least one waiting to be resumed
            DCHECK(m_write_resumers != nullptr);

            // pop off the first resumer
            auto* to_resume  = m_write_resumers;
            m_write_resumers = to_resume->m_next;

            // if to_resume is the only resumer, we can safely symmetric transfer
            if (m_write_resumers == nullptr && to_resume != nullptr)
            {
                read_op->m_resume = to_resume;
                return false;
            }

            // resume the writer
            // note: it would be nice if we can symmetric transfer back to the writer but if we do, and the writer does
            // not write to the channel, we lose our execution context. if we put the awaiting writer_resummers into the
            // equivalent of a task_container, we could yield th reader and resume the task container. writer_resumers
            lock.unlock();
            to_resume->m_awaiting_coroutine.resume();
            lock.lock();
        }

        // if there are no readers and the channel is closed, we should resume immediately
        if (m_closed.load(std::memory_order::acquire)) [[unlikely]]
        {
            read_op->m_channel_closed = true;
            lock.unlock();
            return true;
        }

        // there are no writers present and the channel is still open ==> this reader must suspend
        // the await_suspend method is responsible for unlocking
        return false;
    }

    friend auto tag_invoke(unifex::tag_t<cpo::async_write> _, Immediate& t, T&& data) noexcept -> WriteOperation
    {
        return {t, std::move(data)};
    }

    friend auto tag_invoke(unifex::tag_t<cpo::async_read> _, Immediate& t) noexcept -> ReadOperation
    {
        return {t};
    }

    friend auto tag_invoke(unifex::tag_t<cpo::close> _, Immediate& t) noexcept -> void
    {
        t.close();
    }

    mutex_type m_mutex;
    WriteOperation* m_write_waiters{nullptr};
    WriteOperation* m_write_resumers{nullptr};
    ReadOperation* m_read_waiters{nullptr};
    std::atomic<bool> m_closed{false};
};

template <typename T>
using ImmediateChannel = Channel<Immediate<T>>;  // NOLINT

}  // namespace mrc::channel::v2
