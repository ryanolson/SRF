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

#include "mrc/coroutines/async_generator.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"
#include "mrc/ops/cpo/inputs.hpp"
#include "mrc/ops/input_stream.hpp"
#include "mrc/ops/scheduling_terms/scheduling_term.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"
#include "mrc/utils/macros.hpp"

namespace mrc::ops {

class CountSchedulingTerm : public SchedulingTerm<Tick>
{
  public:
    explicit CountSchedulingTerm(std::size_t max_count) : m_max_count(max_count) {}

  private:
    class InputStream final
    {
      public:
        using data_type = Tick;

        InputStream(std::size_t* counter, const std::size_t& max_count, std::stop_token stop_token) :
          m_counter(counter),
          m_max_count(max_count),
          m_stop_token(std::move(stop_token))
        {}

        std::suspend_never next() noexcept
        {
            (*m_counter)++;
            return {};
        }

        const Tick& data() const
        {
            return m_tick;
        }

        operator bool() const noexcept
        {
            return *m_counter < m_max_count && !m_stop_token.stop_requested();
        }

      private:
        Tick m_tick;
        std::size_t* m_counter;
        std::size_t m_max_count;
        std::stop_token m_stop_token;
    };

    friend auto tag_invoke(unifex::tag_t<cpo::make_input_stream> _,
                           CountSchedulingTerm& term,
                           std::stop_token&& stop_token) -> InputStream
    {
        return {&term.m_counter, term.m_max_count, std::move(stop_token)};
    }

    std::size_t m_counter{0};
    std::size_t m_max_count;
};

static_assert(concepts::scheduling_term<CountSchedulingTerm>);

}  // namespace mrc::ops
