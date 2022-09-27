#include "libsre/trace/runtime_context.hpp"

#include <glog/logging.h>
#include <opentelemetry/context/context.h>

namespace sre::trace {

ContextStack::ContextStack()
{
    m_stack.emplace_front();
}

ContextStack::ContextStack(opentelemetry::context::Context context)
{
    m_stack.push_front(context);
}

opentelemetry::context::Context ContextStack::get_current() noexcept
{
    return m_stack.front();
}

const Context& ContextStack::attach(const opentelemetry::context::Context& context) noexcept
{
    m_stack.push_front(context);
    return m_stack.front();
}

bool ContextStack::detach(opentelemetry::context::Token& token) noexcept
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
