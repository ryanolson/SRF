#include "srf/core/thread.hpp"
#include "srf/coro/ring_buffer.hpp"
#include "srf/coro/sync_wait.hpp"
#include "srf/coro/task.hpp"
#include "srf/coro/thread_pool.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stop_token>
#include <string>
#include <thread>

using namespace srf;

class TestCoroTask : public ::testing::Test
{};

static auto double_task = [](std::uint64_t x) -> coro::Task<std::uint64_t> { co_return x * 2; };

static auto scheduled_task = [](coro::ThreadPool& tp, std::uint64_t x) -> coro::Task<std::uint64_t> {
    co_await tp.schedule();
    co_return x * 2;
};

static auto double_and_add_5_task = [](std::uint64_t input) -> coro::Task<std::uint64_t> {
    auto doubled = co_await double_task(input);
    co_return doubled + 5;
};

TEST_F(TestCoroTask, Task)
{
    auto output = coro::sync_wait(double_task(2));
    EXPECT_EQ(output, 4);
}

TEST_F(TestCoroTask, ScheduledTask)
{
    coro::ThreadPool main({.thread_count = 1, .description = "main"});
    auto output = coro::sync_wait(scheduled_task(main, 2));
    EXPECT_EQ(output, 4);
}

TEST_F(TestCoroTask, Tasks)
{
    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);
}

TEST_F(TestCoroTask, RingBufferStressTest)
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
            }
            co_return;
        };

        coro::sync_wait(coro::when_all(source(), sink()));
    }
}
