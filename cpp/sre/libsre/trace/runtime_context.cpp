#include "libsre/trace/runtime_context.hpp"

#include <glog/logging.h>
#include <opentelemetry/context/context.h>

namespace sre::trace {

CoroutineContextStack::CoroutineContextStack() = default;

CoroutineContextStack::CoroutineContextStack(opentelemetry::context::Context context)
{
    m_stack.push_front(context);
}

opentelemetry::context::Context CoroutineContextStack::get_current() noexcept
{
    if (m_stack.empty())
    {
        return opentelemetry::context::Context{};
    }
    return m_stack.front();
}

const Context& CoroutineContextStack::attach(const opentelemetry::context::Context& context) noexcept
{
    m_stack.push_front(context);
    return m_stack.front();
}

bool CoroutineContextStack::detach(opentelemetry::context::Token& token) noexcept
{
    // In most cases, the context to be detached is on the top of the stack.
    if (token == m_stack.front())
    {
        m_stack.pop_front();
        return true;
    }

    // detemine if the stack contains the token
    if (!std::any_of(m_stack.cbegin(), m_stack.cend(), [&token](const opentelemetry::context::Context& context) {
            return context == token;
        }))
    {
        return false;
    }

    // pop from front until we get to the token of interest
    while (!(token == m_stack.front()))
    {
        m_stack.pop_front();
    }

    m_stack.pop_front();
    return true;
}

}  // namespace sre::trace
