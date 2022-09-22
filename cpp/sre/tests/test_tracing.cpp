#include "test_tracing.hpp"

#include <gtest/gtest.h>

class TelemetryTracing : public ::testing::Test
{};

static void foo()
{
    auto tracer = sre::trace::get_tracer();
    auto span   = tracer->StartSpan("foo");
    auto scope  = tracer->WithActiveSpan(span);

    // do some serious work

    span->End();
}

static void bar()
{
    auto scoped_span = trace_api::Scope(sre::trace::get_tracer()->StartSpan("bar"));
    foo();
    foo();
}

TEST_F(TelemetryTracing, ScopedSpan)
{
    // init_log_tracer();
    // init_external_tracer();
    auto scoped_span = trace_api::Scope(sre::trace::get_tracer()->StartSpan("TelementryTracing.ScopedSpan"));

    bar();
}
