#pragma once
#include <glog/logging.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <stdexcept>
#include <thread>

namespace sre::trace {

class CoroutineRuntimeContextStorage final : public opentelemetry::context::RuntimeContextStorage
{
    CoroutineRuntimeContextStorage()
    {
        m_stack.emplace_front();
    }

    CoroutineRuntimeContextStorage(opentelemetry::context::Context context)
    {
        m_stack.push_front(context);
    }

    static opentelemetry::nostd::shared_ptr<RuntimeContextStorage> create(
        std::shared_ptr<CoroutineRuntimeContextStorage> shared)
    {
        return opentelemetry::nostd::shared_ptr<CoroutineRuntimeContextStorage>(std::move(shared));
    }

  public:
    static opentelemetry::nostd::shared_ptr<RuntimeContextStorage> create()
    {
        LOG(INFO) << "creating CoroutineRuntimeContextStorage";
        auto shared = std::shared_ptr<CoroutineRuntimeContextStorage>(new CoroutineRuntimeContextStorage());
        return CoroutineRuntimeContextStorage::create(std::move(shared));
    }

    static opentelemetry::nostd::shared_ptr<RuntimeContextStorage> create(opentelemetry::context::Context context)
    {
        auto shared = std::shared_ptr<CoroutineRuntimeContextStorage>(new CoroutineRuntimeContextStorage(context));
        return CoroutineRuntimeContextStorage::create(std::move(shared));
    }

    opentelemetry::context::Context GetCurrent() noexcept final
    {
        return m_stack.front();
    }

    opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> Attach(
        const opentelemetry::context::Context& context) noexcept override
    {
        m_stack.push_front(context);
        return CreateToken(context);
    }

    bool Detach(opentelemetry::context::Token& token) noexcept override
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

  private:
    std::deque<opentelemetry::context::Context> m_stack;
};

/**
 * @brief OpenTelemetry does provide us mutable access the RuntimeContextStorage which is required for capturing and
 * restoring context state across coroutine boundaries.
 * We will use this as a proxy where we can hold and manage two RuntimeContextStorage objects: primary thread context
 * and current context.
 */
class RuntimeContext
{
  public:
    using context_type = opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage>;

    // when a coroutine yields, it should restore the default runtime context
    static context_type swap_current_context_to_primary_context()
    {
        context_type current = m_current_context;
        m_current_context    = primary_thread_context();
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(primary_thread_context());
        return current;
    }

    // when a coroutine resumes, it should use this method to set the thread_local context to the context it owns
    static void set_current_context_to_external_context(context_type context)
    {
        m_current_context = context;
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(context);
    }

    // when we create a coroutine, we can create new context from the current context
    static context_type make_coroutine_context_from_current_context()
    {
        // a coroutine does not need a copy of the context stack, since the coroutine will never pop beyond the current
        static thread_local auto initialized = init();
        return CoroutineRuntimeContextStorage::create(m_current_context->GetCurrent());
    }

    static bool using_primary_context()
    {
        return primary_thread_context().get() == m_current_context.get();
    }

    static context_type init()
    {
        m_current_context = primary_thread_context();
        return m_current_context;
    }

  private:
    static context_type& primary_thread_context()
    {
        static thread_local context_type context = CoroutineRuntimeContextStorage::create();
        return context;
    }

    static thread_local context_type m_current_context;
};

}  // namespace sre::trace
