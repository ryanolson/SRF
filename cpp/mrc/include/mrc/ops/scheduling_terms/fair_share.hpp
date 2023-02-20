/**
 * SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/cpo/scheduling_term.hpp"
#include "mrc/ops/input_stream.hpp"
#include "mrc/ops/scheduling_terms/scheduling_term.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

#include <coroutine>

namespace mrc::ops {

class FairShare : public SchedulingTerm<Tick>
{
  public:
    class InputStream;

    class SchedulingOperation
    {
      public:
        bool await_ready() const noexcept
        {
            return m_ticks != 0;
        }

        auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> void {}

        constexpr static auto await_resume() noexcept -> void {}

      private:
        SchedulingOperation(FairShare& parent, std::size_t ticks) : m_parent(parent), m_ticks(ticks) {}

        FairShare& m_parent;
        std::size_t m_ticks;

        friend InputStream;
    };

    class InputStream final
    {
      public:
        using data_type = Tick;

        auto next() noexcept -> SchedulingOperation
        {
            m_ticks -= 1;
            return {m_parent, m_ticks};
        }

        const Tick& data() const
        {
            return m_tick;
        }

        operator bool() const noexcept
        {
            return m_ticks != 0 && !m_stop_token.stop_requested();
        }

      private:
        InputStream(FairShare& parent, std::size_t ticks, std::stop_token stop_token) :
          m_parent(parent),
          m_ticks(ticks),
          m_stop_token(std::move(stop_token))
        {}

        FairShare& m_parent;
        std::size_t m_ticks;
        std::stop_token m_stop_token;
        Tick m_tick;

        friend FairShare;
    };

  private:
    static_assert(concepts::input_stream<InputStream>);

    friend auto tag_invoke(unifex::tag_t<cpo::get_input_stream> _, FairShare& scheduler, std::size_t ticks) noexcept
        -> InputStream
    {
        return {scheduler, ticks, scheduler.m_stop_source.get_token()};
    }

    std::stop_source m_stop_source;
};

}  // namespace mrc::ops
