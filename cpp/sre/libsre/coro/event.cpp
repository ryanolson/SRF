#include "sre/coro/event.hpp"

#include "sre/coro/thread_local_state.hpp"
#include "sre/coro/thread_pool.hpp"

namespace sre::coro {

auto Event::Awaiter::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> bool
{
    const void* const set_state = &m_event;

    m_awaiting_coroutine = awaiting_coroutine;

    // This value will update if other threads write to it via acquire.
    void* old_value = m_event.m_state.load(std::memory_order::acquire);
    do
    {
        // Resume immediately if already in the set state.
        if (old_value == set_state)
        {
            return false;
        }

        m_next = static_cast<Awaiter*>(old_value);
    } while (!m_event.m_state.compare_exchange_weak(
        old_value, this, std::memory_order::release, std::memory_order::acquire));

    ThreadLocalState::suspend_coro_thread_local_state();
    return true;
}

auto Event::Awaiter::await_resume() noexcept -> void
{
    ThreadLocalState::resume_coro_thread_local_state();
}

void Event::Awaiter::resume()
{
    if (thread_pool() != nullptr)
    {
        thread_pool()->resume(m_awaiting_coroutine);
    }
    else
    {
        ThreadLocalState::suspend_coro_thread_local_state();
        m_awaiting_coroutine.resume();
        ThreadLocalState::resume_coro_thread_local_state();
    }
}

Event::Event(bool initially_set) noexcept : m_state((initially_set) ? static_cast<void*>(this) : nullptr) {}

auto Event::set(ResumeOrderPolicy policy) noexcept -> void
{
    // Exchange the state to this, if the state was previously not this, then traverse the list
    // of awaiters and resume their coroutines.
    void* old_value = m_state.exchange(this, std::memory_order::acq_rel);
    if (old_value != this)
    {
        // If FIFO has been requsted then reverse the order upon resuming.
        if (policy == ResumeOrderPolicy::fifo)
        {
            old_value = reverse(static_cast<Awaiter*>(old_value));
        }
        // else lifo nothing to do

        auto* waiters = static_cast<Awaiter*>(old_value);
        while (waiters != nullptr)
        {
            auto* next = waiters->m_next;
            // waiters->m_awaiting_coroutine.resume();
            waiters->resume();
            waiters = next;
        }
    }
}

auto Event::reverse(Awaiter* curr) -> Awaiter*
{
    if (curr == nullptr || curr->m_next == nullptr)
    {
        return curr;
    }

    Awaiter* prev = nullptr;
    Awaiter* next = nullptr;
    while (curr != nullptr)
    {
        next         = curr->m_next;
        curr->m_next = prev;
        prev         = curr;
        curr         = next;
    }

    return prev;
}

auto Event::reset() noexcept -> void
{
    void* old_value = this;
    m_state.compare_exchange_strong(old_value, nullptr, std::memory_order::acquire);
}

}  // namespace sre::coro
