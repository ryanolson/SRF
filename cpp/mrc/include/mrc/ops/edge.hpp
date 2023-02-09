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

#pragma once

#include "mrc/channel/v2/concepts/channel.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/scheduling_terms/on_next_data.hpp"

#include <coroutine>
#include <map>
#include <mutex>
#include <optional>

namespace mrc::ops {

template <core::concepts::data DataT>
struct EdgeWritable
{
    virtual ~EdgeWritable() = default;

    virtual void set_generator(coroutines::AsyncGenerator<DataT>&& generator) = 0;
};

template <core::concepts::data DataT>
struct EdgeReadable
{
    virtual ~EdgeReadable() = default;

    struct GetOperation
    {
        virtual bool await_ready()                                    = 0;
        virtual void await_suspend(std::coroutine_handle<> coroutine) = 0;
        virtual coroutines::AsyncGenerator<DataT> await_resume()      = 0;
    };

    virtual GetOperation get_generator() = 0;
};

template <core::concepts::data DataT>
class Edge : public EdgeWritable<DataT>, public EdgeReadable<DataT>
{
  public:
    using data_type = DataT;

  private:
    enum class State
    {
        Initialized,
        Loaded,
        Delivered
    };

    class GetOperationImpl : public EdgeReadable<DataT>::GetOperation
    {
      public:
        bool await_ready() final
        {
            return static_cast<bool>(m_parent.m_state == State::Loaded);
        }

        void await_suspend(std::coroutine_handle<> coroutine) final
        {
            auto lock                = std::move(m_lock);
            m_coroutine              = coroutine;
            m_parent.m_get_operation = this;
        }

        coroutines::AsyncGenerator<DataT> await_resume() final
        {
            auto lock = std::move(m_lock);
            CHECK(m_parent.m_state == State::Loaded);
            m_parent.m_state = State::Delivered;
            return std::move(m_parent.m_generator);
        }

      private:
        GetOperationImpl(Edge& parent) : m_parent(parent), m_lock(m_parent.m_mutex) {}

        Edge& m_parent;
        std::coroutine_handle<> m_coroutine;
        std::unique_lock<std::mutex> m_lock;
        friend Edge;
    };

    void set_generator(coroutines::AsyncGenerator<DataT>&& generator) final
    {
        std::lock_guard lock(m_mutex);
        CHECK(m_state == State::Initialized);
        m_state     = State::Loaded;
        m_generator = std::move(generator);
    }

    [[nodiscard]] GetOperationImpl get_generator() final
    {
        return {*this};
    }

    State m_state{State::Initialized};
    coroutines::AsyncGenerator<DataT> m_generator;
    GetOperationImpl* m_get_operation{nullptr};
    std::mutex m_mutex;
};

}  // namespace mrc::ops
