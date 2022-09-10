/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "coro/event.hpp"
#include "coro/ring_buffer.hpp"
#include "coro/ring_buffer_dynamic.hpp"
#include "coro/sync_wait.hpp"
#include "coro/task_container.hpp"
#include "coro/thread_pool.hpp"
#include "coro/when_all.hpp"
#include "expected/expected.hpp"

#include "internal/coroutines/system_resources.hpp"

#include "srf/options/options.hpp"
#include "srf/utils/macros.hpp"

#include <coro/coro.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <chrono>
#include <concepts>  // IWYU pragma: keep
#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

static auto make_thread_pool(std::string name, std::uint32_t thread_count)
{
    return std::make_shared<coro::thread_pool>(coro::thread_pool::options{
        // By default all thread pools will create its thread count with the
        // std::thread::hardware_concurrency() as the number of worker threads in the pool,
        // but this can be changed via this thread_count option.  This example will use 4.
        .thread_count = thread_count,
        // Upon starting each worker thread an optional lambda callback with the worker's
        // index can be called to make thread changes, perhaps priority or change the thread's
        // name.
        .on_thread_start_functor = [name](std::size_t worker_idx) -> void {
            std::cout << name << ": thread pool worker " << worker_idx << " is starting up.\n";
        },
        // Upon stopping each worker thread an optional lambda callback with the worker's
        // index can b called.
        .on_thread_stop_functor = [name](std::size_t worker_idx) -> void {
            std::cout << name << ":thread pool worker " << worker_idx << " is shutting down.\n";
        }});
}

static auto make_io_scheduler()
{
    return std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
        // The scheduler will spawn a dedicated event processing thread.  This is the default, but
        // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
        .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
        // If the scheduler is in spawn mode this functor is called upon starting the dedicated
        // event processor thread.
        .on_io_thread_start_functor =
            [] { LOG(INFO) << "io_scheduler::process event thread start on " << std::this_thread::get_id(); },
        // If the scheduler is in spawn mode this functor is called upon stopping the dedicated
        // event process thread.
        .on_io_thread_stop_functor =
            [] { LOG(INFO) << "io_scheduler::process event thread stop on: " << std::this_thread::get_id(); },
        // The io scheduler uses a coro::thread_pool to process the events or tasks it is given.
        // The tasks are not processed inline on the dedicated event processor thread so events can
        // be received and handled as soon as a worker thread is available.  See the coro::thread_pool
        // for the available options and their descriptions.
        .pool = coro::thread_pool::options{.thread_count = 1,
                                           .on_thread_start_functor =
                                               [](size_t i) {
                                                   LOG(INFO) << "io_scheduler::thread_pool worker " << i << " starting "
                                                             << " on thread " << std::this_thread::get_id();
                                               },
                                           .on_thread_stop_functor =
                                               [](size_t i) {
                                                   LOG(INFO) << "io_scheduler::thread_pool worker " << i << " stopping "
                                                             << " on thread " << std::this_thread::get_id();
                                               }}});
}

static std::shared_ptr<srf::internal::system::System> make_system(std::function<void(srf::Options&)> updater = nullptr)
{
    auto options = std::make_shared<srf::Options>();
    if (updater)
    {
        updater(*options);
    }

    return srf::internal::system::make_system(std::move(options));
}

static auto make_main()
{
    return std::make_shared<coro::io_scheduler>(coro::io_scheduler::options{
        // The scheduler will spawn a dedicated event processing thread.  This is the default, but
        // it is possible to use 'manual' and call 'process_events()' to drive the scheduler yourself.
        .thread_strategy = coro::io_scheduler::thread_strategy_t::manual,
        // The io scheduler uses a coro::thread_pool to process the events or tasks it is given.
        // The tasks are not processed inline on the dedicated event processor thread so events can
        // be received and handled as soon as a worker thread is available.  See the coro::thread_pool
        // for the available options and their descriptions.
        .pool = coro::thread_pool::options{.thread_count = 1,
                                           .on_thread_start_functor =
                                               [](size_t i) {
                                                   LOG(INFO) << "io_scheduler::thread_pool worker " << i << " starting "
                                                             << " on thread " << std::this_thread::get_id();
                                               },
                                           .on_thread_stop_functor =
                                               [](size_t i) {
                                                   LOG(INFO) << "io_scheduler::thread_pool worker " << i << " stopping "
                                                             << " on thread " << std::this_thread::get_id();
                                               }}});
}

class TestCpp20 : public ::testing::Test
{};

TEST_F(TestCpp20, Task)
{
    auto double_task = [](uint64_t x) -> coro::task<uint64_t> { co_return x * 2; };

    auto double_and_add_5_task = [&](uint64_t input) -> coro::task<uint64_t> {
        auto doubled = co_await double_task(input);
        co_return doubled + 5;
    };

    auto output = coro::sync_wait(double_and_add_5_task(2));
    EXPECT_EQ(output, 9);
}

template <coro::concepts::executor executor_type>
class task_queue
{
    template <typename result_type>
    class task_result final
    {
      public:
        using return_type = tl::expected<result_type, std::exception_ptr>;

        task_result()  = default;
        ~task_result() = default;

        DELETE_COPYABILITY(task_result)
        DELETE_MOVEABILITY(task_result);

        auto result() -> coro::task<return_type>
        {
            co_await m_event;
            co_return std::move(m_result);
        }

      private:
        coro::event m_event;
        return_type m_result;
        friend task_queue;
    };

  public:
    // options
    struct options
    {};

    // constructor
    task_queue(std::shared_ptr<executor_type> executor, const options ops = options{}) :
      m_executor(executor),
      m_task_container(m_executor)
    {}
    ~task_queue() = default;

    auto run() -> coro::task<void>
    {
        co_await m_executor->schedule();

        while (true)
        {
            auto expected = co_await m_ring_buffer.consume();
            if (!expected)
            {
                break;
            }
            m_task_container.start(std::move(*expected));
        }

        co_await m_task_container.garbage_collect_and_yield_until_empty();
        co_return;
    }

    auto stop() -> coro::task<void>
    {
        if (!m_running.load(std::memory_order::acquire))
        {
            co_return;
        }

        m_running.exchange(false, std::memory_order::acquire);

        co_await m_ring_buffer.produce([this]() -> coro::task<void> {
            while (!m_ring_buffer.empty())
            {
                co_await m_executor->yield();
            }
            m_ring_buffer.notify_waiters();
            co_return;
        }());

        co_return;
    }

    void progress()
    {
        m_task_container.garbage_collect();
    }

    template <typename result_type>
    auto enqueue(coro::task<result_type>&& task) -> coro::task<std::shared_ptr<task_result<result_type>>>
    {
        using return_type = tl::expected<result_type, std::exception_ptr>;
        using state_type  = std::shared_ptr<task_result<result_type>>;

        if (!is_running())
        {
            LOG(FATAL) << "task queue stopped";
        }

        // run the task, capture the result and complete the event
        auto worker_task = [this](coro::task<result_type> task, state_type state) -> coro::task<void> {
            try
            {
                if constexpr (std::is_same_v<result_type, void>)
                {
                    co_await task;
                    state->m_result = {};
                }
                else
                {
                    state->m_result = co_await task;
                }
            } catch (...)
            {
                state->m_result = tl::make_unexpected(std::current_exception());
            }
            state->m_event.set();
            co_return;
        };

        // shared state with the coro::event to synchronize the worker and the future task
        auto state = std::make_shared<task_result<result_type>>();

        // start the task in the task contaienr
        co_await m_ring_buffer.produce(worker_task(std::move(task), state));

        // return the future_task to await on the result of the worker_task
        co_return state;
    }

    bool is_running() const
    {
        return m_running.load(std::memory_order::acquire);
    }

  private:
    std::shared_ptr<executor_type> m_executor;
    coro::task_container<executor_type> m_task_container;
    coro::ring_buffer<coro::task<void>, 128> m_ring_buffer;
    std::atomic<bool> m_running{true};
};

class system_resources
{};

TEST_F(TestCpp20, TaskQueue)
{
    auto tp = make_thread_pool("main", 1);
    task_queue tq{tp};

    LOG(INFO) << "test thread: " << std::this_thread::get_id();

    auto app = [&]() -> coro::task<void> {
        auto make_task = []() -> coro::task<int> {
            LOG(INFO) << "hello from " << std::this_thread::get_id();
            co_return 42;
        };

        LOG(INFO) << "enqueue work";
        auto future = co_await tq.enqueue(make_task());
        auto result = co_await future->result();

        EXPECT_TRUE(result);
        EXPECT_EQ(*result, 42);
        LOG(INFO) << "got the result: " << *result;

        co_await tq.stop();
        co_return;
    };

    coro::sync_wait(coro::when_all(tq.run(), app()));
}

TEST_F(TestCpp20, ThreadPoolDiscoverable)
{
    auto tp = make_thread_pool("main", 1);
    EXPECT_EQ(coro::thread_pool::on_this_thread(), nullptr);

    auto test_on_thread_pool = [&tp]() -> coro::task<void> {
        EXPECT_EQ(coro::thread_pool::on_this_thread(), nullptr);
        co_await tp->schedule();
        EXPECT_NE(coro::thread_pool::on_this_thread(), nullptr);
        co_await coro::thread_pool::on_this_thread()->schedule();
    };

    coro::sync_wait(test_on_thread_pool());
}

// TEST_F(TestCpp20, TheadPool)
// {
//     LOG(INFO) << "main thread: " << std::this_thread::get_id();

//     using task_t = coro::task<void>;

//     auto main    = make_thread_pool("main", 1);
//     auto workers = make_thread_pool("workers", 2);
//     coro::ring_buffer<task_t, 16> rb{};
//     coro::event shutdown_event;

//     auto make_task_launcher = [&]() -> coro::task<void> {
//         co_await main->schedule();
//         coro::task_container tc{main};

//         LOG(INFO) << "my task_handler on thread: " << std::this_thread::get_id();

//         while (true)
//         {
//             auto expected = co_await rb.consume();
//             if (!expected)
//             {
//                 break;
//             }
//             tc.start(std::move(*expected));
//         }

//         co_await tc.garbage_collect_and_yield_until_empty();
//         co_return;
//     };

//     auto program = [&]() -> coro::task<void> {
//         LOG(INFO) << "zzzzzz on " << std::this_thread::get_id();

//         auto enqueue = [&]() -> coro::task<std::shared_ptr<state>> {
//             auto shared = std::make_shared<state>();
//             auto task   = [&](std::shared_ptr<state> shared) -> coro::task<void> {
//                 LOG(INFO) << "start task on " << std::this_thread::get_id();
//                 co_await main->yield();
//                 shared->value = 42;
//                 shared->event.set();
//                 LOG(INFO) << "end task on " << std::this_thread::get_id();
//                 co_return;
//             };
//             co_await rb.produce(task(shared));
//             co_return shared;
//         };

//         auto task = [&]() -> coro::task<void> {
//             LOG(INFO) << "start task on " << std::this_thread::get_id();
//             // co_await workers->schedule();
//             LOG(INFO) << "end task on " << std::this_thread::get_id();
//             co_return;
//         };

//         LOG(INFO) << "about to enqueue tasks";

//         auto f1 = co_await enqueue();
//         auto f2 = co_await enqueue();
//         auto f3 = co_await enqueue();
//         auto f4 = co_await enqueue();

//         co_await(f1->event);
//         co_await(f2->event);
//         co_await(f3->event);
//         co_await(f4->event);

//         while (!rb.empty())
//         {
//             std::this_thread::yield();
//         }
//         rb.notify_waiters();

//         co_return;
//     };

//     coro::sync_wait(coro::when_all(make_task_launcher(), program()));
// }

TEST_F(TestCpp20, TestYieldFromExternalThreadPool)
{
    auto io = make_io_scheduler();
    auto tp = make_thread_pool("tp", 1);

    auto get_tp_id = [&]() -> coro::task<std::thread::id> {
        co_await tp->schedule();
        co_return std::this_thread::get_id();
    };

    auto get_tp_id_after_yield = [&]() -> coro::task<std::thread::id> {
        co_await tp->schedule();
        co_await io->yield_for(std::chrono::milliseconds(100));
        co_return std::this_thread::get_id();
    };

    auto test = [&]() -> coro::task<void> {
        co_await io->schedule();
        auto io_id = std::this_thread::get_id();
        auto tp_id = co_await get_tp_id();
        EXPECT_NE(tp_id, io_id);
        auto after = co_await get_tp_id_after_yield();
        EXPECT_EQ(tp_id, after);

        auto on_event_loop = [&]() -> coro::task<void> {
            auto id = std::this_thread::get_id();
            EXPECT_NE(io_id, id);
            co_await io->yield();
            auto id_after_yield = std::this_thread::get_id();
            EXPECT_EQ(id, id_after_yield);
        };

        io->schedule(std::move(on_event_loop()));
    };

    coro::sync_wait(test());
}

TEST_F(TestCpp20, NonCoro)
{
    auto task = [&] {
        LOG(INFO) << "start task on " << std::this_thread::get_id();
        // co_await workers->schedule();
        LOG(INFO) << "end task on " << std::this_thread::get_id();
    };

    task();
    task();
    task();
    task();
}

template <typename T>
concept vector_like = requires(T t)
{
    t.begin();
    t.reserve(1);
    t.data();
};

static_assert(vector_like<std::vector<int>>);
static_assert(!vector_like<int>);

template <typename T>
concept smart_ptr_like = requires(T t)
{
    t.operator*();
    t.operator->();
    t.release();
    t.reset();
    typename T::element_type;

    requires !std::copyable<T>;

    // clang-format off
    { t.operator->() } -> std::same_as<typename std::add_pointer<typename T::element_type>::type>;
    { *t } -> std::same_as<typename std::add_lvalue_reference<typename T::element_type>::type>;
    // clang-format on
};

static_assert(smart_ptr_like<std::unique_ptr<int>>);
static_assert(!smart_ptr_like<std::shared_ptr<int>>);
static_assert(!smart_ptr_like<int>);

class RunnablePrototype
{
    struct operation;

    auto initialize() -> operation;
    auto start() -> operation;
    auto live() -> operation;
    auto stop() -> operation;
    auto kill() -> operation;
    auto join() -> operation;
};

TEST_F(TestCpp20, TaskContainer)
{
    auto tp = make_thread_pool("tp", 1);
    coro::task_container tc{tp};

    auto app = [&tp]() -> coro::task<void> {
        LOG(INFO) << "my async app";
        co_return;
    };

    tc.start(app());
    LOG(INFO) << "sleep test thread";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    LOG(INFO) << "done test thread sleep";
    coro::sync_wait(tc.garbage_collect_and_yield_until_empty());
}

TEST_F(TestCpp20, TaskResume)
{
    auto tp = make_thread_pool("tp", 1);
    // coro::task_container tc{tp};

    auto app = [&tp]() -> coro::task<void> {
        co_await tp->schedule();
        LOG(INFO) << "my async app";
        co_return;
    }();

    app.resume();

    // tc.start(app());
    LOG(INFO) << "sleep test thread";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    LOG(INFO) << "done test thread sleep";
    // coro::sync_wait(tc.garbage_collect_and_yield_until_empty());
    coro::sync_wait(app);
}

// class Context
// {};

// class RunnableProto
// {
//     void start(Context context)
//     {
//         m_task = [this]() -> coro::task<void> {
//             co_await initialize(context);
//             co_await run(context);
//             co_return;
//         }();
//     }

//     [[nodiscard]] bool stop_requested() const noexcept
//     {
//         return m_stop_source.stop_requested();
//     }

//     [[nodiscard]] bool kill_requested() const noexcept
//     {
//         return m_kill_source.stop_requested();
//     }

//   private:
//     virtual coro::task<void> initialize(Context& context) = 0;
//     virtual coro::task<void> run(Context& context)        = 0;

//     coro::task<void> m_task;
//     std::stop_source m_stop_source;
//     std::stop_source m_kill_source;
// };

TEST_F(TestCpp20, SystemResources)
{
    auto system    = make_system();
    auto resources = std::make_unique<srf::internal::coroutines::SystemResources>(system, 0);
}

TEST_F(TestCpp20, RingBufferDynamicRescheduleConsumer)
{
    auto p_tp = make_thread_pool("producer", 1);
    auto c_tp = make_thread_pool("consumer", 1);

    coro::ring_buffer_dynamic<int> rb({.capacity = 2, .consumer_policy = decltype(rb)::schedule_policy_t::reschedule});

    auto consumer = [&]() -> coro::task<void> {
        co_await c_tp->schedule();
        auto c_tid = std::this_thread::get_id();

        auto i = co_await rb.consume();

        auto c_tid_after = std::this_thread::get_id();
        EXPECT_EQ(c_tid, c_tid_after);
        EXPECT_TRUE(i);
        EXPECT_TRUE(*i == 42);

        co_return;
    };

    auto producer = [&]() -> coro::task<void> {
        co_await p_tp->schedule();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        co_await rb.produce(42);
        co_return;
    };

    coro::sync_wait(coro::when_all(consumer(), producer()));
}

TEST_F(TestCpp20, RingBufferDynamicImmediatelyResumeConsumer)
{
    auto p_tp = make_thread_pool("producer", 1);
    auto c_tp = make_thread_pool("consumer", 1);

    coro::ring_buffer_dynamic<int> rb(coro::ring_buffer_dynamic<int>::options{
        .capacity = 2, .consumer_policy = coro::ring_buffer_dynamic<int>::schedule_policy_t::immediate});

    auto consumer = [&]() -> coro::task<void> {
        co_await p_tp->schedule();
        auto p_tid = std::this_thread::get_id();
        co_await c_tp->schedule();
        auto c_tid = std::this_thread::get_id();

        auto i = co_await rb.consume();

        auto c_tid_after = std::this_thread::get_id();
        EXPECT_NE(c_tid, c_tid_after);
        EXPECT_EQ(p_tid, c_tid_after);
        EXPECT_TRUE(i);
        EXPECT_TRUE(*i == 42);

        co_return;
    };

    auto producer = [&]() -> coro::task<void> {
        co_await p_tp->schedule();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        co_await rb.produce(42);
        co_return;
    };

    coro::sync_wait(coro::when_all(consumer(), producer()));
}

TEST_F(TestCpp20, RingBufferDynamicImmediatelyResumeProducer)
{
    EXPECT_TRUE(false);
}

TEST_F(TestCpp20, RingBufferDynamicRescheduleProducer)
{
    EXPECT_TRUE(false);
}

TEST_F(TestCpp20, ScratchPad) {}
