#include "sre/coro/thread_local_state.hpp"

#include "libsre/trace/runtime_context.hpp"

#include <opentelemetry/context/runtime_context.h>

namespace sre::coro {

void ThreadLocalState::create_coro_thread_local_state()
{
    m_context_stack = trace::RuntimeContext::make_context();
}

void ThreadLocalState::suspend_coro_thread_local_state()
{
    m_context_stack = trace::RuntimeContext::suspend_context();
}

void ThreadLocalState::resume_coro_thread_local_state()
{
    if (m_context_stack)
    {
        trace::RuntimeContext::resume_context(std::move(m_context_stack));
    }
}

}  // namespace sre::coro
