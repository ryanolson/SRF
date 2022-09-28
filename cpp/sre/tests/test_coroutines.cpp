#include "libsre/trace/runtime_context.hpp"
#include "test_tracing.hpp"

#include "sre/coro/ring_buffer.hpp"
#include "sre/coro/sync_wait.hpp"
#include "sre/coro/task.hpp"
#include "sre/coro/thread_pool.hpp"
#include "sre/system/thread.hpp"
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

#include <chrono>
#include <stop_token>
#include <string>
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


static auto double_task = [](std::uint64_t x) -> coro::Task<std::uint64_t> {
    EXPECT_FALSE(trace::RuntimeContext::using_default_context());
    auto scope = trace::Scope(trace::get_tracer()->StartSpan("double_task"));
    co_return x * 2;
};

static auto scheduled_task = [](coro::ThreadPool& tp, std::uint64_t x) -> coro::Task<std::uint64_t> {
    co_await tp.schedule();
    EXPECT_FALSE(trace::RuntimeContext::using_default_context());
    auto scope = trace::Scope(trace::get_tracer()->StartSpan("double_task"));
    co_return x * 2;
};

static auto double_and_add_5_task = [](std::uint64_t input) -> coro::Task<std::uint64_t> {
    EXPECT_FALSE(trace::RuntimeContext::using_default_context());
    auto scope   = trace::Scope(trace::get_tracer()->StartSpan("double + 5 task"));
    auto doubled = co_await double_task(input);
    co_return doubled + 5;
};

TEST_F(Coroutines, Task)
{
    auto output = coro::sync_wait(double_task(2));
    EXPECT_EQ(output, 4);
}

TEST_F(Coroutines, ScheduledTask)
{
    coro::ThreadPool main({.thread_count = 1, .description = "main"});
    auto output = coro::sync_wait(scheduled_task(main, 2));
    EXPECT_EQ(output, 4);
}

TEST_F(Coroutines, Tasks)
{
    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);
}

TEST_F(Coroutines, TracedTasks)
{
    auto span_data = init_in_memory_tracing();

    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);

    EXPECT_TRUE(trace::RuntimeContext::using_default_context());
    EXPECT_EQ(span_data->GetSpans().size(), 2);
}

TEST_F(Coroutines, ThreadID)
{
    coro::ThreadPool unnamed({.thread_count = 1});
    coro::ThreadPool main({.thread_count = 1, .description = "main"});

    auto log_id = [](coro::ThreadPool& tp) -> coro::Task<std::string> {
        co_await tp.schedule();
        co_return sre::this_thread::get_id();
    };

    auto from_main    = coro::sync_wait(log_id(main));
    auto from_unnamed = coro::sync_wait(log_id(unnamed));

    VLOG(1) << sre::this_thread::get_id();
    VLOG(1) << from_main;
    VLOG(1) << from_unnamed;

    EXPECT_TRUE(sre::this_thread::get_id().starts_with("sys"));
    EXPECT_TRUE(from_main.starts_with("main"));
    EXPECT_TRUE(from_unnamed.starts_with("thread_pool"));
}

TEST_F(Coroutines, RingBuffer)
{
    coro::ThreadPool writer({.thread_count = 1, .description = "writer"});
    coro::ThreadPool reader({.thread_count = 1, .description = "reader"});
    coro::RingBuffer<std::unique_ptr<std::uint64_t>> buffer({.capacity = 2});

    for (int iters = 16; iters <= 16; iters++)
    {
        auto source = [&writer, &buffer, iters]() -> coro::Task<void> {
            co_await writer.schedule();
            for (std::uint64_t i = 0; i < iters; i++)
            {
                co_await buffer.write(std::make_unique<std::uint64_t>(i));
            }
            co_return;
        };

        auto sink = [&reader, &buffer, iters]() -> coro::Task<void> {
            co_await reader.schedule();
            for (std::uint64_t i = 0; i < iters; i++)
            {
                auto unique = co_await buffer.read();
                EXPECT_TRUE(unique);
                EXPECT_EQ(*(unique.value()), i);
                // LOG(INFO) << iters << ": " << i;
            }
            co_return;
        };

        coro::sync_wait(coro::when_all(source(), sink()));
    }
}

TEST_F(Coroutines, TracedRingBuffer)
{
    // init_external_tracer();
    // init_log_tracer();

    auto tracer = trace::get_tracer();
    auto span   = tracer->StartSpan("Test: Coroutines.RingBuffer");
    auto scope  = tracer->WithActiveSpan(span);

    LOG(INFO) << "main thread context: " << tracer->GetCurrentSpan().get();

    coro::ThreadPool writer({.thread_count = 1, .description = "writer"});
    coro::ThreadPool reader({.thread_count = 1, .description = "reader"});
    coro::RingBuffer<std::unique_ptr<std::uint64_t>> buffer({.capacity = 2});

    constexpr std::uint64_t iters = 8;  // NOLINT

    auto source = [&writer, &buffer, &tracer, iters]() -> coro::Task<void> {
        auto span  = tracer->StartSpan("source");
        auto scope = tracer->WithActiveSpan(span);
        co_await writer.schedule();
        for (std::uint64_t i = 0; i < iters; i++)
        {
            auto span  = tracer->StartSpan("on_next");
            auto scope = tracer->WithActiveSpan(span);
            co_await buffer.write(std::make_unique<std::uint64_t>(i));
        }
        co_return;
    };

    auto sink = [&reader, &buffer, &tracer, iters]() -> coro::Task<void> {
        auto span  = tracer->StartSpan("sink");
        auto scope = tracer->WithActiveSpan(span);
        co_await reader.schedule();
        for (std::uint64_t i = 0; i < iters; i++)
        {
            auto ptr   = co_await buffer.read();
            auto span  = tracer->StartSpan("sink_on_data");
            auto scope = tracer->WithActiveSpan(span);
            EXPECT_TRUE(ptr);
            EXPECT_EQ(*(ptr.value()), i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(source(), sink()));
}
