#include "sre/coro/thread_local_state.hpp"

#include "libsre/trace/runtime_context.hpp"

#include <opentelemetry/context/runtime_context.h>

namespace sre::coro {

void ThreadLocalState::create_coro_thread_local_state()
{
    m_context_stack = trace::RuntimeContext::make_context();
    m_should_resume = true;
}

void ThreadLocalState::suspend_coro_thread_local_state()
{
    m_context_stack = trace::RuntimeContext::suspend_context();
    m_should_resume = true;
}

void ThreadLocalState::resume_coro_thread_local_state()
{
    if (m_should_resume)
    {
        trace::RuntimeContext::resume_context(std::move(m_context_stack));
    }
}

}  // namespace sre::coro
