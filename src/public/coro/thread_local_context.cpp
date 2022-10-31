#include "srf/coro/thread_local_context.hpp"

#include "srf/coro/thread_pool.hpp"

#include <cuda_runtime_api.h>

namespace srf::coro {

void ThreadLocalContext::suspend_thread_local_context()
{
    // suspend the srf context
    m_thread_pool   = ThreadPool::from_current_thread();
    m_should_resume = true;
}

void ThreadLocalContext::resume_thread_local_context()
{
    if (m_should_resume)
    {
        // resume the srf context
        m_should_resume = false;
    }
}

void ThreadLocalContext::resume_coroutine(std::coroutine_handle<> coroutine)
{
    if (m_thread_pool != nullptr)
    {
        // add event - scheduled on
        m_thread_pool->resume(coroutine);
        return;
    }

    // add a span since the current execution context will be suspended and the coroutine will be resumed
    ThreadLocalContext ctx;
    ctx.suspend_thread_local_context();
    coroutine.resume();
    ctx.resume_thread_local_context();
}

void ThreadLocalContext::set_resume_on_thread_pool(ThreadPool* thread_pool)
{
    m_thread_pool = thread_pool;
}

ThreadPool* ThreadLocalContext::thread_pool() const
{
    return m_thread_pool;
}

}  // namespace srf::coro
