
#include "sre/coro/event.hpp"
#include "sre/coro/sync_wait.hpp"
#include "sre/coro/task.hpp"
#include "sre/coro/thread_pool.hpp"
#include "sre/coro/when_all.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sre;

class Event : public ::testing::Test
{};

TEST_F(Event, LifeCycle)
{
    coro::Event e{};

    auto func = [&]() -> coro::Task<uint64_t> {
        co_await e;
        co_return 42;
    };

    auto task = func();

    task.resume();
    EXPECT_FALSE(task.is_ready());
    e.set();  // this will automaticaly resume the task that is awaiting the event.
    EXPECT_TRUE(task.is_ready());
    EXPECT_TRUE(task.promise().result() == 42);
}

auto producer(coro::Event& event) -> void
{
    // Long running task that consumers are waiting for goes here...
    event.set();
}

auto consumer(const coro::Event& event) -> coro::Task<uint64_t>
{
    co_await event;
    // Normally consume from some object which has the stored result from the producer
    co_return 42;
}

// TEST_CASE("event one watcher", "[event]")
TEST_F(Event, SingleWatcher)
{
    coro::Event e{};

    auto value = consumer(e);
    value.resume();  // start co_awaiting event
    EXPECT_FALSE(value.is_ready());

    producer(e);

    EXPECT_TRUE(value.promise().result() == 42);
}

// TEST_CASE("event multiple watchers", "[event]")
TEST_F(Event, MultipleWatchers)
{
    coro::Event e{};

    auto value1 = consumer(e);
    auto value2 = consumer(e);
    auto value3 = consumer(e);
    value1.resume();  // start co_awaiting event
    value2.resume();
    value3.resume();
    EXPECT_FALSE(value1.is_ready());
    EXPECT_FALSE(value2.is_ready());
    EXPECT_FALSE(value3.is_ready());

    producer(e);

    EXPECT_TRUE(value1.promise().result() == 42);
    EXPECT_TRUE(value2.promise().result() == 42);
    EXPECT_TRUE(value3.promise().result() == 42);
}

// TEST_CASE("event reset", "[event]")
TEST_F(Event, Reset)
{
    coro::Event e{};

    e.reset();
    EXPECT_FALSE(e.is_set());

    auto value1 = consumer(e);
    value1.resume();  // start co_awaiting event
    EXPECT_FALSE(value1.is_ready());

    producer(e);
    EXPECT_TRUE(value1.promise().result() == 42);

    e.reset();

    auto value2 = consumer(e);
    value2.resume();
    EXPECT_FALSE(value2.is_ready());

    producer(e);

    EXPECT_TRUE(value2.promise().result() == 42);
}

// TEST_CASE("event fifo", "[event]")
TEST_F(Event, FIFO)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter = [&](uint64_t value) -> coro::Task<void> {
        co_await tp.schedule();
        co_await e;

        counter++;
        EXPECT_TRUE(counter == value);

        co_return;
    };

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(
        coro::when_all(make_waiter(1), make_waiter(2), make_waiter(3), make_waiter(4), make_waiter(5), make_setter()));

    EXPECT_TRUE(counter == 5);
}

// TEST_CASE("event fifo none", "[event]")
TEST_F(Event, FIFO_None)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_setter()));

    EXPECT_TRUE(counter == 0);
}

// TEST_CASE("event fifo single", "[event]")
TEST_F(Event, FIFO_Single)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter = [&](uint64_t value) -> coro::Task<void> {
        co_await tp.schedule();
        co_await e;

        counter++;
        EXPECT_TRUE(counter == value);

        co_return;
    };

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_waiter(1), make_setter()));

    EXPECT_TRUE(counter == 1);
}

// TEST_CASE("event fifo executor", "[event]")
TEST_F(Event, FIFO_Executor)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter = [&](uint64_t value) -> coro::Task<void> {
        co_await tp.schedule();
        co_await e;

        counter++;
        EXPECT_TRUE(counter == value);

        co_return;
    };

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(tp, coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(
        coro::when_all(make_waiter(1), make_waiter(2), make_waiter(3), make_waiter(4), make_waiter(5), make_setter()));

    EXPECT_TRUE(counter == 5);
}

// TEST_CASE("event fifo none executor", "[event]")
TEST_F(Event, FIFO_NoExecutor)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(tp, coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_setter()));

    EXPECT_TRUE(counter == 0);
}

// TEST_CASE("event fifo single executor", "[event]")
TEST_F(Event, FIFO_SingleExecutor)
{
    coro::Event e{};

    // Need consistency FIFO on a single thread to verify the execution order is correct.
    coro::ThreadPool tp{coro::ThreadPool::Options{.thread_count = 1}};

    std::atomic<uint64_t> counter{0};

    auto make_waiter = [&](uint64_t value) -> coro::Task<void> {
        co_await tp.schedule();
        co_await e;

        counter++;
        EXPECT_TRUE(counter == value);

        co_return;
    };

    auto make_setter = [&]() -> coro::Task<void> {
        co_await tp.schedule();
        EXPECT_TRUE(counter == 0);
        e.set(tp, coro::ResumeOrderPolicy::fifo);
        co_return;
    };

    coro::sync_wait(coro::when_all(make_waiter(1), make_setter()));

    EXPECT_TRUE(counter == 1);
}
