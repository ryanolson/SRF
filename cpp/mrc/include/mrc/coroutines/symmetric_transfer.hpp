/**
 * SPDX-FileCopyrightText: Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#pragma once

#include "mrc/coroutines/latch.hpp"

#include <coroutine>
#include <memory>
#include <type_traits>

namespace mrc::coroutines {

template <typename T>
class SymmetricTransfer
{
  public:
    class InitialOperation;
    class SwapOperation;

    using data_type = std::decay_t<T>;

    operator bool() const noexcept
    {
        return (m_data != nullptr);
    }

    [[nodiscard]] SwapOperation async_write(data_type& data) noexcept
    {
        m_data = std::addressof(data);
        return SwapOperation{*this};
    }

    // used by writer
    [[nodiscard]] SwapOperation async_write(data_type&& data) noexcept
    {
        m_data = std::addressof(data);
        return SwapOperation{*this};
    }

    // used by writer
    [[nodiscard]] auto wait_until_initialized() noexcept -> Event::Awaiter
    {
        return m_latch.operator co_await();
    }

    // used by reader
    [[nodiscard]] InitialOperation initialize() noexcept
    {
        return {*this};
    }

    // used by reader
    [[nodiscard]] SwapOperation async_read() noexcept
    {
        return SwapOperation{*this};
    }

    // used by the writer to shutdown the reader
    // shold not be called by the reader
    void close() noexcept
    {
        m_data = nullptr;
        if (m_coroutine && !m_coroutine.done())
        {
            m_coroutine.resume();
        }
    }

    T* data() noexcept
    {
        return m_data;
    }

    class InitialOperation
    {
      public:
        InitialOperation(SymmetricTransfer& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> reader) noexcept
        {
            m_parent.m_coroutine = reader;
            m_parent.m_latch.count_down();
        }

        constexpr static void await_resume() noexcept {}

      private:
        SymmetricTransfer& m_parent;
    };

    class SwapOperation
    {
      public:
        SwapOperation(SymmetricTransfer& parent) : m_parent(parent) {}

        constexpr static bool await_ready() noexcept
        {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> reader) noexcept
        {
            return std::exchange(m_parent.m_coroutine, reader);
        }

        constexpr static void await_resume() noexcept {}

      private:
        SymmetricTransfer& m_parent;
    };

  private:
    data_type* m_data{nullptr};
    std::coroutine_handle<> m_coroutine;
    Latch m_latch{1};
};

}  // namespace mrc::coroutines
