#include "libsre/trace/runtime_context.hpp"
#include "sre/coro/sync_wait.hpp"
#include "sre/coro/task.hpp"
#include "sre/trace/trace.hpp"

#include <gtest/gtest.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <thread>

using namespace opentelemetry::sdk::trace;
using namespace opentelemetry::sdk::common;
using opentelemetry::exporter::memory::InMemorySpanData;
using opentelemetry::exporter::memory::InMemorySpanExporter;
using opentelemetry::trace::SpanContext;
using opentelemetry::trace::TracerProvider;

using namespace sre;

class Coroutines : public ::testing::Test
{};

static std::shared_ptr<InMemorySpanData> init_in_memory_tracing()
{
    std::unique_ptr<InMemorySpanExporter> exporter(new InMemorySpanExporter());
    std::shared_ptr<InMemorySpanData> span_data = exporter->GetData();

    auto processor                           = SimpleSpanProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<TracerProvider> provider = TracerProviderFactory::Create(std::move(processor));

    opentelemetry::trace::Provider::SetTracerProvider(provider);

    return span_data;
}

static auto double_task = [](std::uint64_t x) -> coro::Task<std::uint64_t> {
    EXPECT_FALSE(trace::RuntimeContext::using_primary_context());
    auto scope = trace::Scope(trace::get_tracer()->StartSpan("double_task"));
    co_return x * 2;
};

static auto double_and_add_5_task = [](std::uint64_t input) -> coro::Task<std::uint64_t> {
    EXPECT_FALSE(trace::RuntimeContext::using_primary_context());
    auto scope   = trace::Scope(trace::get_tracer()->StartSpan("double + 5 task"));
    auto doubled = co_await double_task(input);
    co_return doubled + 5;
};

TEST_F(Coroutines, Task)
{
    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);
}

TEST_F(Coroutines, TracedTasks)
{
    auto span_data = init_in_memory_tracing();

    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);

    EXPECT_TRUE(trace::RuntimeContext::using_primary_context());
    EXPECT_EQ(span_data->GetSpans().size(), 2);
}

TEST_F(Coroutines, ThreadID)
{
    auto id = std::this_thread::get_id();

    std::stringstream ss;
    ss << id;
    auto str_id = ss.str();

    auto hash_id = std::hash<decltype(id)>()(id);
    std::stringstream sss;
    sss << hash_id;
    auto str_hash = sss.str();

    LOG(INFO) << "id: " << str_id << "; hash: " << str_hash;
    EXPECT_TRUE(str_id == str_hash);
}
