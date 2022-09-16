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
    CoroutineRuntimeContextStorage();
    CoroutineRuntimeContextStorage(opentelemetry::context::Context context);

  public:
    static opentelemetry::nostd::shared_ptr<RuntimeContextStorage> create();
    static opentelemetry::nostd::shared_ptr<RuntimeContextStorage> create(opentelemetry::context::Context context);

    opentelemetry::context::Context GetCurrent() noexcept final;

    opentelemetry::nostd::unique_ptr<opentelemetry::context::Token> Attach(
        const opentelemetry::context::Context& context) noexcept override;

    bool Detach(opentelemetry::context::Token& token) noexcept override;

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
        context_type current = current_context();
        current_context()    = primary_thread_context();
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(primary_thread_context());
        return current;
    }

    // when a coroutine resumes, it should use this method to set the thread_local context to the context it owns
    static void set_current_context_to_external_context(context_type context)
    {
        current_context() = context;
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(context);
    }

    // when we create a coroutine, we can create new context from the current context
    static context_type make_coroutine_context_from_current_context()
    {
        // a coroutine does not need a copy of the context stack, since the coroutine will never pop beyond the current
        return CoroutineRuntimeContextStorage::create(current_context()->GetCurrent());
    }

    static bool using_primary_context()
    {
        return primary_thread_context().get() == current_context().get();
    }

  private:
    inline static context_type& current_context()
    {
        static thread_local context_type current_context = primary_thread_context();
        return current_context;
    }

    inline static context_type& primary_thread_context()
    {
        static thread_local context_type context = CoroutineRuntimeContextStorage::create();
        return context;
    }
};

}  // namespace sre::trace
