#include "libsre/trace/runtime_context.hpp"

namespace sre::trace {

opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage> CoroutineRuntimeContextStorage::create()
{
    DVLOG(10) << "creating CoroutineRuntimeContextStorage";
    return opentelemetry::nostd::shared_ptr<CoroutineRuntimeContextStorage>(new CoroutineRuntimeContextStorage());
}
opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage> CoroutineRuntimeContextStorage::create(
    opentelemetry::context::Context context)
{
    DVLOG(10) << "creating CoroutineRuntimeContextStorage with current context";
    return opentelemetry::nostd::shared_ptr<CoroutineRuntimeContextStorage>(
        new CoroutineRuntimeContextStorage(context));
}
CoroutineRuntimeContextStorage::CoroutineRuntimeContextStorage()
{
    m_stack.emplace_front();
}
CoroutineRuntimeContextStorage::CoroutineRuntimeContextStorage(opentelemetry::context::Context context)
{
    m_stack.push_front(context);
}
opentelemetry::context::Context CoroutineRuntimeContextStorage::GetCurrent() noexcept
{
    return m_stack.front();
}
opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> CoroutineRuntimeContextStorage::Attach(
    const opentelemetry::context::Context& context) noexcept
{
    m_stack.push_front(context);
    return CreateToken(context);
}
bool CoroutineRuntimeContextStorage::Detach(opentelemetry::context::Token& token) noexcept
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
