
#include "./telemetry/runtime_context.hpp"
#include "sre/expected.hpp"

#include <opentelemetry/context/runtime_context.h>

namespace sre {

Expected<Success> init()
{
    auto runtime_context = telemetry::CoroutineRuntimeContextStorage::Create();
    opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(runtime_context);

    return Success{};
}

}  // namespace sre
