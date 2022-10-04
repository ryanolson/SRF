#include "sre/coro/thread_local_state.hpp"

#include "libsre/trace/runtime_context.hpp"

#include "sre/coro/thread_pool.hpp"

#include <opentelemetry/context/runtime_context.h>

namespace sre::coro {

void ThreadLocalState::create_coro_thread_local_state()
{
    m_context_stack = trace::RuntimeContext::make_context();
    m_should_resume = true;
}

void ThreadLocalState::suspend_coro_thread_local_state()
{
    // when we suspsend, we should not have a tracing context stack
    DCHECK(!m_context_stack);
    m_context_stack = trace::RuntimeContext::suspend_context();
    m_thread_pool   = ThreadPool::from_current_thread();
    m_should_resume = true;
}

void ThreadLocalState::resume_coro_thread_local_state()
{
    if (m_should_resume)
    {
        trace::RuntimeContext::resume_context(std::move(m_context_stack));
    }
}

void ThreadLocalState::resume_coroutine(std::coroutine_handle<> coroutine)
{
    if (m_thread_pool != nullptr)
    {
        // add event - scheduled on
        m_thread_pool->resume(coroutine);
        return;
    }

    // add a span since the current execution context will be suspended and the coroutine will be resumed
    ThreadLocalState current_state;
    current_state.suspend_coro_thread_local_state();
    coroutine.resume();
    current_state.resume_coro_thread_local_state();
}

void ThreadLocalState::set_resume_on_thread_pool(ThreadPool* thread_pool)
{
    m_thread_pool = thread_pool;
}

ThreadPool* ThreadLocalState::thread_pool()
{
    return m_thread_pool;
}

}  // namespace sre::coro
