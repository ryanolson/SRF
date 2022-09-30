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

#include "sre/trace/context_stack.hpp"

#include <memory>

namespace sre::coro {

class ThreadPool;

/**
 * @brief The SRE Runtime has several third-party dependencies that make use of thread_local storage. Because
 * coroutines can yield execution, other coroutines running on the same thread might modify the thread local storage
 * which would have non-deterministic consequences for the resuming coroutine. Since coroutines can also migrate to
 * other threads, it's important for the awaiter to capture any thread local state so it can be restored regardless of
 * where the coroutine is resumed.
 *
 * This class captures the thread_local state for CUDA and OpenTelemetry.
 */
class ThreadLocalState
{
  protected:
    // use when creating a new coroutine task or initializing a promise_type
    void create_coro_thread_local_state();

    // use when suspending a coroutine
    void suspend_coro_thread_local_state();

    // use when resuming a coroutine
    void resume_coro_thread_local_state();

    // if not nullptr, represents the thread pool on which the caller to suspend was executing
    ThreadPool* thread_pool();

  private:
    bool m_should_resume{false};
    int m_cuda_device_id{0};
    ThreadPool* m_thread_pool{nullptr};
    std::unique_ptr<trace::ContextStack> m_context_stack{nullptr};
};

}  // namespace sre::coro
