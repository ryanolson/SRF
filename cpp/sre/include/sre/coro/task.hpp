#pragma once

#include "sre/coro/concepts/promise.hpp"
#include "sre/coro/thread_local_state.hpp"

#include <glog/logging.h>

#include <coroutine>
#include <exception>
#include <utility>

namespace sre::coro {

template <typename ReturnT = void>
class Task;

namespace detail {
struct PromiseBase
{
    friend struct FinalAwaitable;

    struct InitialAwaitable : ThreadLocalState
    {
        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            ThreadLocalState::create_coro_thread_local_state();
        }

        void await_resume() const noexcept
        {
            ThreadLocalState::resume_coro_thread_local_state();
        }
    };

    struct FinalAwaitable : ThreadLocalState
    {
        constexpr static auto await_ready() noexcept -> bool
        {
            return false;
        }

        template <typename PromiseT>
        auto await_suspend(std::coroutine_handle<PromiseT> coroutine) noexcept -> std::coroutine_handle<>
        {
            ThreadLocalState::suspend_coro_thread_local_state();

            // If there is a continuation call it, otherwise this is the end of the line.
            auto& promise = coroutine.promise();
            if (promise.m_continuation != nullptr)
            {
                return promise.m_continuation;
            }

            // on suspend, we restore the primary thread state
            // if on this yield, a coroutine is resumed, that coroutine will restore the its thread state
            return std::noop_coroutine();
        }

        // on the final awaitable, we do not resume the captured thread local state
        constexpr static auto await_resume() noexcept -> void {}
    };

    PromiseBase() noexcept = default;
    ~PromiseBase()         = default;

    static auto initial_suspend() -> InitialAwaitable
    {
        return {};
    }

    static auto final_suspend() noexcept(true) -> FinalAwaitable
    {
        return {};
    }

    auto unhandled_exception() -> void
    {
        m_exception_ptr = std::current_exception();
    }

    auto continuation(std::coroutine_handle<> continuation) noexcept -> void
    {
        m_continuation = continuation;
    }

  protected:
    std::coroutine_handle<> m_continuation{nullptr};
    std::exception_ptr m_exception_ptr{};
};

template <typename ReturnT>
struct Promise final : public PromiseBase
{
    using task_type      = Task<ReturnT>;
    using coroutine_type = std::coroutine_handle<Promise<ReturnT>>;

    Promise() noexcept = default;
    ~Promise()         = default;

    auto get_return_object() noexcept -> task_type;

    auto return_value(ReturnT value) -> void
    {
        m_return_value = std::move(value);
    }

    auto result() const& -> const ReturnT&
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }

        return m_return_value;
    }

    auto result() && -> ReturnT&&
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }

        return std::move(m_return_value);
    }

  private:
    ReturnT m_return_value;
};

template <>
struct Promise<void> : public PromiseBase
{
    using task_type      = Task<void>;
    using coroutine_type = std::coroutine_handle<Promise<void>>;

    Promise() noexcept = default;
    ~Promise()         = default;

    auto get_return_object() noexcept -> task_type;

    auto return_void() noexcept -> void {}

    auto result() -> void
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }
    }
};

}  // namespace detail

template <typename ReturnT>
class [[nodiscard]] Task
{
  public:
    using task_type      = Task<ReturnT>;
    using promise_type   = detail::Promise<ReturnT>;
    using coroutine_type = std::coroutine_handle<promise_type>;

    struct AwaitableBase
    {
        AwaitableBase(coroutine_type coroutine) noexcept : m_coroutine(coroutine) {}

        auto await_ready() const noexcept -> bool
        {
            return !m_coroutine || m_coroutine.done();
        }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            m_coroutine.promise().continuation(awaiting_coroutine);
            return m_coroutine;
        }

        std::coroutine_handle<promise_type> m_coroutine{nullptr};
    };

    Task() noexcept : m_coroutine(nullptr) {}

    explicit Task(coroutine_type handle) : m_coroutine(handle) {}
    Task(const Task&) = delete;
    Task(Task&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

    ~Task()
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
        }
    }

    auto operator=(const Task&) -> Task& = delete;

    auto operator=(Task&& other) noexcept -> Task&
    {
        if (std::addressof(other) != this)
        {
            if (m_coroutine != nullptr)
            {
                m_coroutine.destroy();
            }

            m_coroutine = std::exchange(other.m_coroutine, nullptr);
        }

        return *this;
    }

    /**
     * @return True if the Task is in its final suspend or if the Task has been destroyed.
     */
    auto is_ready() const noexcept -> bool
    {
        return m_coroutine == nullptr || m_coroutine.done();
    }

    auto resume() -> bool
    {
        if (!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto destroy() -> bool
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    auto operator co_await() const& noexcept
    {
        struct Awaitable : public AwaitableBase
        {
            auto await_resume() -> decltype(auto)
            {
                if constexpr (std::is_same_v<void, ReturnT>)
                {
                    // Propagate uncaught exceptions.
                    this->m_coroutine.promise().result();
                    return;
                }
                else
                {
                    return this->m_coroutine.promise().result();
                }
            }
        };

        return Awaitable{m_coroutine};
    }

    auto operator co_await() const&& noexcept
    {
        struct Awaitable : public AwaitableBase
        {
            auto await_resume() -> decltype(auto)
            {
                if constexpr (std::is_same_v<void, ReturnT>)
                {
                    // Propagate uncaught exceptions.
                    this->m_coroutine.promise().result();
                    return;
                }
                else
                {
                    return std::move(this->m_coroutine.promise()).result();
                }
            }
        };

        return Awaitable{m_coroutine};
    }

    auto promise() & -> promise_type&
    {
        return m_coroutine.promise();
    }

    auto promise() const& -> const promise_type&
    {
        return m_coroutine.promise();
    }
    auto promise() && -> promise_type&&
    {
        return std::move(m_coroutine.promise());
    }

    auto handle() -> coroutine_type
    {
        return m_coroutine;
    }

  private:
    coroutine_type m_coroutine{nullptr};
};

namespace detail {
template <typename ReturnT>
inline auto Promise<ReturnT>::get_return_object() noexcept -> Task<ReturnT>
{
    return Task<ReturnT>{coroutine_type::from_promise(*this)};
}

inline auto Promise<void>::get_return_object() noexcept -> Task<>
{
    return Task<>{coroutine_type::from_promise(*this)};
}

}  // namespace detail

}  // namespace sre::coro
