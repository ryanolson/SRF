
#include "sre/coro/latch.hpp"
#include "sre/coro/ring_buffer.hpp"
#include "sre/coro/schedule_policy.hpp"
#include "sre/coro/sync_wait.hpp"
#include "sre/coro/task.hpp"
#include "sre/coro/when_all.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sre;

class RingBuffer : public ::testing::Test
{};

// // TEST_CASE("ring_buffer zero num_elements", "[ring_buffer]")
// {
//     REQUIRE_THROWS(coro::ring_buffer<uint64_t, 0>{});
// }

// TEST_CASE("ring_buffer single element", "[ring_buffer]")
TEST_F(RingBuffer, SingleElement)
{
    const size_t iterations = 10;
    coro::RingBuffer<uint64_t> rb{{.capacity = 1}};

    std::vector<uint64_t> output{};

    auto make_producer_task = [&]() -> coro::Task<void> {
        for (size_t i = 1; i <= iterations; ++i)
        {
            // std::cerr << "produce: " << i << "\n";
            co_await rb.write(i);
        }
        co_return;
    };

    auto make_consumer_task = [&]() -> coro::Task<void> {
        for (size_t i = 1; i <= iterations; ++i)
        {
            auto expected = co_await rb.read();
            auto value    = std::move(*expected);

            // std::cerr << "consume: " << value << "\n";
            output.emplace_back(std::move(value));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(make_producer_task(), make_consumer_task()));

    for (size_t i = 1; i <= iterations; ++i)
    {
        EXPECT_TRUE(output[i - 1] == i);
    }

    EXPECT_TRUE(rb.empty());
}

TEST_F(RingBuffer, WriteX5ThenClose)
{
    const size_t iterations = 5;
    coro::RingBuffer<uint64_t> rb{{.capacity = 2}};

    std::vector<uint64_t> output{};

    auto make_producer_task = [&]() -> coro::Task<void> {
        for (size_t i = 1; i <= iterations; ++i)
        {
            EXPECT_FALSE(rb.is_closed());
            co_await rb.write(i);
        }
        rb.close();
        EXPECT_TRUE(rb.is_closed());
        auto status = co_await rb.write(42);
        EXPECT_EQ(status, coro::RingBuffer<uint64_t>::WriteResult::Stopped);
        co_return;
    };

    auto make_consumer_task = [&]() -> coro::Task<void> {
        while (true)
        {
            auto expected = co_await rb.read();

            if (!expected)
            {
                break;
            }
            auto value = std::move(*expected);
            output.emplace_back(std::move(value));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(make_producer_task(), make_consumer_task()));

    for (size_t i = 1; i <= iterations; ++i)
    {
        EXPECT_TRUE(output[i - 1] == i);
    }

    EXPECT_TRUE(rb.empty());
    EXPECT_TRUE(rb.is_closed());
}

TEST_F(RingBuffer, FullyBufferedWriteX5ThenClose)
{
    const size_t iterations = 5;
    coro::RingBuffer<uint64_t> rb{{.capacity = 16}};
    coro::Latch latch{iterations + 1};

    std::vector<uint64_t> output{};

    auto make_producer_task = [&]() -> coro::Task<void> {
        for (size_t i = 1; i <= iterations; ++i)
        {
            EXPECT_FALSE(rb.is_closed());
            co_await rb.write(i);
            latch.count_down();
        }
        rb.close();
        EXPECT_TRUE(rb.is_closed());
        auto status = co_await rb.write(42);
        EXPECT_EQ(status, coro::RingBuffer<uint64_t>::WriteResult::Stopped);
        latch.count_down();
        co_return;
    };

    auto make_consumer_task = [&]() -> coro::Task<void> {
        co_await latch;
        while (true)
        {
            auto expected = co_await rb.read();

            if (!expected)
            {
                break;
            }
            auto value = std::move(*expected);
            output.emplace_back(std::move(value));
        }
        co_return;
    };

    coro::sync_wait(coro::when_all(make_producer_task(), make_consumer_task()));

    for (size_t i = 1; i <= iterations; ++i)
    {
        EXPECT_TRUE(output[i - 1] == i);
    }

    EXPECT_TRUE(rb.empty());
    EXPECT_TRUE(rb.is_closed());
}

// TEST_CASE("ring_buffer many elements many producers many consumers", "[ring_buffer]")
TEST_F(RingBuffer, MultiProducerMultiConsumer)
{
    const size_t iterations = 1'000'000;
    const size_t consumers  = 100;
    const size_t producers  = 100;

    coro::ThreadPool tp{{.thread_count = 4}};
    coro::RingBuffer<uint64_t> rb{{.capacity = 64}};
    coro::Latch producers_latch{producers};

    auto make_producer_task = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        auto to_produce = iterations / producers;

        for (size_t i = 1; i <= to_produce; ++i)
        {
            switch (i % 3)
            {
            case 0:
                co_await rb.write(i);
                break;
            case 1:
                co_await rb.write(i).resume_immediately();
                break;
            case 2:
                co_await rb.write(i).resume_on(&tp);
                break;
            }
        }

        producers_latch.count_down();

        co_return;
    };

    auto make_consumer_task = [&]() -> coro::Task<void> {
        co_await tp.schedule();

        while (true)
        {
            auto expected = co_await rb.read();
            if (!expected)
            {
                break;
            }

            auto item = std::move(*expected);
            (void)item;

            co_await tp.yield();  // mimic some work
        }

        co_return;
    };

    auto make_shutdown_task = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        co_await producers_latch;
        rb.close();
        co_return;
    };

    std::vector<coro::Task<void>> tasks{};
    tasks.reserve(consumers + producers + 1);

    tasks.emplace_back(make_shutdown_task());

    for (size_t i = 0; i < consumers; ++i)
    {
        tasks.emplace_back(make_consumer_task());
    }
    for (size_t i = 0; i < producers; ++i)
    {
        tasks.emplace_back(make_producer_task());
    }

    coro::sync_wait(coro::when_all(std::move(tasks)));

    EXPECT_TRUE(rb.empty());
}
