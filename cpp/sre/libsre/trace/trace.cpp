#include "sre/trace/trace.hpp"

#include "libsre/trace/runtime_context.hpp"
#include "sre/version.hpp"

#include <glog/logging.h>
#include <opentelemetry/context/context.h>
#include <opentelemetry/trace/provider.h>

#include <thread>

namespace sre::trace {

namespace detail {

class TracerImpl
{
  public:
    static Handle<Tracer> get_tracer()
    {
        // ensure we replace the default runtime context on each thread
        static thread_local auto init = RuntimeContext::init();

        auto provider = opentelemetry::trace::Provider::GetTracerProvider();
        return provider->GetTracer("libsre", SRE_VERSION);
    }
};

}  // namespace detail

Handle<Tracer> get_tracer()
{
    return detail::TracerImpl::get_tracer();
}

}  // namespace sre::trace
