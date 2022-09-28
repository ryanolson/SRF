#pragma once

#include "sre/common/macros.hpp"
#include "sre/trace/context_stack.hpp"

#include <glog/logging.h>
#include <opentelemetry/common/macros.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sre::trace {

class CoroutineRuntimeContextStorage final : public opentelemetry::context::RuntimeContextStorage
{
  public:
    Context GetCurrent() noexcept final
    {
        return get_storage().get_current();
    }

    opentelemetry::nostd::unique_ptr<Token> Attach(const Context& context) noexcept final
    {
        return CreateToken(get_storage().attach(context));
    }

    bool Detach(Token& token) noexcept final
    {
        return get_storage().detach(token);
    }

    [[nodiscard]] static std::unique_ptr<ContextStack> suspend_context(
        std::unique_ptr<ContextStack> next_stack = nullptr)
    {
        auto current     = std::move(external_stack());
        external_stack() = std::move(next_stack);
        return current;
    }

    static bool using_default_context()
    {
        return !external_stack();
    }

    static void resume_context(std::unique_ptr<ContextStack> stack)
    {
        if (external_stack())
        {
            LOG(INFO) << "external stack has a value";
        }
        CHECK(!external_stack());
        external_stack() = std::move(stack);
    }

  private:
    static inline ContextStack& get_storage()
    {
        if (external_stack())
        {
            return *external_stack();
        }
        return default_stack();
    }

    static inline ContextStack& default_stack()
    {
        static thread_local ContextStack stack;
        return stack;
    }

    static inline std::unique_ptr<ContextStack>& external_stack()
    {
        static thread_local std::unique_ptr<ContextStack> stack{nullptr};
        return stack;
    }
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
    using storage_type = CoroutineRuntimeContextStorage;
    using stack_type   = std::unique_ptr<ContextStack>;

    // when a coroutine yields, it should restore the default runtime context
    [[nodiscard]] static stack_type suspend_context(stack_type new_stack = nullptr)
    {
        return get_storage().suspend_context(std::move(new_stack));
    }

    // when a coroutine resumes, it should use this method to set the thread_local context to the context it owns
    static void resume_context(stack_type context)
    {
        get_storage().resume_context(std::move(context));
    }

    // when we create a coroutine, we can create new context from the current context
    static stack_type make_context()
    {
        // a coroutine does not need a copy of the context stack, since the coroutine will never pop beyond the current
        return std::make_unique<ContextStack>(get_storage().GetCurrent());
    }

    static bool init()
    {
        get_storage();
        return true;
    }

    static bool using_default_context()
    {
        return get_storage().using_default_context();
    }

  private:
    RuntimeContext()
    {
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(internal_init());
    }

    inline static context_type& internal_init()
    {
        static context_type context(new CoroutineRuntimeContextStorage);
        return context;
    }

    inline static storage_type& get_storage()
    {
        static RuntimeContext runtime;
        static storage_type& context = *static_cast<CoroutineRuntimeContextStorage*>(internal_init().get());
        return context;
    }
};

}  // namespace sre::trace
