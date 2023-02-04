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

/**
 * Original Source: https://github.com/jbaldwin/libcoro
 * Original License: Apache License, Version 2.0; included below
 */

/**
 * Copyright 2021 Josh Baldwin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mrc/coroutines/thread_pool.hpp"

#include "mrc/core/thread.hpp"
#include "mrc/coroutines/thread_local_context.hpp"

#include <glog/logging.h>

#include <cstddef>
#include <iostream>
#include <sstream>

namespace mrc::coroutines {

ThreadPool::ThreadPool(Options opts) : m_opts(std::move(opts))
{
    if (m_opts.description.empty())
    {
        std::stringstream ss;
        ss << "thread_pool_" << this;
        m_opts.description = ss.str();
    }

    m_threads.reserve(m_opts.thread_count);

    for (uint32_t i = 0; i < m_opts.thread_count; ++i)
    {
        m_threads.emplace_back([this, i](std::stop_token st) {
            executor(std::move(st), i);
        });
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

auto ThreadPool::schedule() -> Operation
{
    if (!m_shutdown_requested.load(std::memory_order::relaxed))
    {
        m_size.fetch_add(1, std::memory_order::release);
        return Operation{*this};
    }

    throw std::runtime_error("coroutines::ThreadPool is shutting down, unable to schedule new tasks.");
}

auto ThreadPool::resume(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    m_size.fetch_add(1, std::memory_order::release);
    schedule_coroutine(handle);
}

auto ThreadPool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (!m_shutdown_requested.exchange(true, std::memory_order::acq_rel))
    {
        for (auto& thread : m_threads)
        {
            thread.request_stop();
        }

        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
}

auto ThreadPool::executor(std::stop_token stop_token, std::size_t idx) -> void
{
    on_thread_start(idx);

    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    while (!stop_token.stop_requested())
    {
        // Wait until the queue has operations to execute or shutdown has been requested.
        while (true)
        {
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.wait(lk, stop_token, [this] {
                return !m_queue.empty();
            });
            if (m_queue.empty())
            {
                lk.unlock();  // would happen on scope destruction, but being explicit/faster(?)
                break;
            }

            auto handle = m_queue.front();
            m_queue.pop_front();

            lk.unlock();  // Not needed for processing the coroutine.

            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto ThreadPool::schedule_operation(Operation* operation) noexcept -> void
{
    schedule_coroutine(operation->m_awaiting_coroutine);
}

auto ThreadPool::schedule_coroutine(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    {
        std::scoped_lock lk{m_wait_mutex};
        m_queue.emplace_back(handle);
    }

    m_wait_cv.notify_one();
}

const std::string& ThreadPool::description() const
{
    return m_opts.description;
}

}  // namespace mrc::coroutines
