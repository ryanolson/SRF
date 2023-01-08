#pragma once

#include "mrc/core/expected.hpp"
#include "mrc/utils/macros.hpp"

#include <glog/logging.h>

#include <coroutine>
#include <memory>

namespace mrc::ops::output {

template <typename T>
class Output
{
  public:
    using data_type = std::decay_t<T>;

    virtual ~Output()                    = default;
    virtual void write(data_type&& data) = 0;

  protected:
};

template <typename T>
class OutputChannel : public Output<T>
{
  public:
    using data_type = std::decay_t<T>;

    void write(T&& data) final
    {
        m_pointer = std::addressof(data);
        m_downstream.resume();
    }

  protected:
    std::suspend_always read();

  private:
    data_type* m_pointer;
    std::coroutine_handle<> m_downstream;
};

}  // namespace mrc::ops::output

namespace mrc::ops {

template <typename T>
class Handoff final
{
  public:
    struct Sentinel
    {};

    Handoff() = default;

    DELETE_COPYABILITY(Handoff);
    DELETE_MOVEABILITY(Handoff);

    struct WriteOperation
    {
        WriteOperation(Handoff& parent, T&& data) : m_parent(parent), m_data(std::move(data)) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            m_suspended_coroutine = awaiting_coroutine;
            m_parent.m_write_op   = this;

            if (m_parent.m_read_op != nullptr)
            {
                m_parent.m_read_op->m_data = std::move(m_data);
                auto coroutine             = m_parent.m_read_op->m_suspended_coroutine;
                m_parent.m_read_op         = nullptr;
                return coroutine;
            }

            return std::noop_coroutine();
        }

        constexpr static auto await_resume() noexcept -> void {}

        Handoff& m_parent;
        T m_data;
        std::coroutine_handle<> m_suspended_coroutine;
    };

    struct ReadOperation
    {
        ReadOperation(Handoff& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            m_suspended_coroutine = awaiting_coroutine;
            m_parent.m_read_op    = this;

            if (m_parent.m_write_op != nullptr)
            {
                auto coroutine      = m_parent.m_write_op->m_suspended_coroutine;
                m_parent.m_write_op = nullptr;
                return coroutine;
            }

            return std::noop_coroutine();
        }

        expected<T, Sentinel> await_resume() noexcept
        {
            if (m_closed) [[unlikely]]
            {
                return unexpected(Sentinel{});
            }
            return {std::move(m_data)};
        }

        Handoff& m_parent;
        T m_data;
        std::coroutine_handle<> m_suspended_coroutine;
        bool m_closed{false};
    };

    WriteOperation write(T&& data)
    {
        return {*this, std::move(data)};
    }

    ReadOperation read()
    {
        return {*this};
    }

    void close()
    {
        if (m_read_op != nullptr && m_read_op->m_suspended_coroutine)
        {
            m_read_op->m_closed = true;
            m_read_op->m_suspended_coroutine.resume();
        }
    }

    WriteOperation* m_write_op{nullptr};
    ReadOperation* m_read_op{nullptr};
};  // namespace mrc::ops

}  // namespace mrc::ops
