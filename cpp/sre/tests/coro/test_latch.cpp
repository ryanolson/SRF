
#include "sre/coro/event.hpp"
#include "sre/coro/latch.hpp"
#include "sre/coro/task.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sre;

class Latch : public ::testing::Test
{};

TEST_F(Latch, Count0)
{
    coro::Latch l{0};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        co_await l;
        co_return 42;
    };

    auto task = make_task();

    task.resume();

    EXPECT_TRUE(task.is_ready());
    EXPECT_EQ(task.promise().result(), 42);
}

TEST_F(Latch, Count1)
{
    coro::Latch l{1};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down();
    EXPECT_TRUE(task.is_ready());
    EXPECT_EQ(task.promise().result(), 1);
}

TEST_F(Latch, Count1Down5)
{
    coro::Latch l{1};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(5);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 1);
}

// TEST_CASE("latch count=5 count_down=1 x5", "[latch]")
TEST_F(Latch, Count5Down1x5)
{
    coro::Latch l{5};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_FALSE(task.is_ready());
    l.count_down(1);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 5);
}

// TEST_CASE("latch count=5 count_down=5", "[latch]")
TEST_F(Latch, Count5Down5)
{
    coro::Latch l{5};

    auto make_task = [&]() -> coro::Task<uint64_t> {
        auto workers = l.remaining();
        co_await l;
        co_return workers;
    };

    auto task = make_task();

    task.resume();
    EXPECT_FALSE(task.is_ready());

    l.count_down(5);
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 5);
}
