#include "libsre/trace/runtime_context.hpp"

namespace sre::trace {

thread_local RuntimeContext::context_type RuntimeContext::m_current_context;

}
