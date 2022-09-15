#include "sre/coro/thread_local_state.hpp"

#include "libsre/trace/runtime_context.hpp"

#include <opentelemetry/context/runtime_context.h>

namespace sre::coro {

void ThreadLocalState::create_coro_thread_local_state()
{
    m_runtime_context = trace::RuntimeContext::make_coroutine_context_from_current_context();
}

void ThreadLocalState::suspend_coro_thread_local_state()
{
    m_runtime_context = trace::RuntimeContext::swap_current_context_to_primary_context();
}

void ThreadLocalState::resume_coro_thread_local_state() const
{
    trace::RuntimeContext::set_current_context_to_external_context(m_runtime_context);
}

}  // namespace sre::coro
