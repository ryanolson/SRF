#pragma once

#include <opentelemetry/context/runtime_context.h>

#include <memory>

namespace sre::coro {

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
    void resume_coro_thread_local_state() const;

  private:
    int m_cuda_device_id{0};
    opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage> m_runtime_context{nullptr};
};

}  // namespace sre::coro
