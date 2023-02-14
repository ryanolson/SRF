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

namespace mrc::ops {

class AlwaysReady : public SchedulingTerm<Tick>
{
    class InputStream final
    {
      public:
        using data_type = Tick;

        InputStream(std::stop_token stop_token) : m_stop_token(std::move(stop_token)) {}

        constexpr static std::suspend_never next() noexcept
        {
            return {};
        }

        const Tick& data() const
        {
            return m_tick;
        }

        operator bool() const noexcept
        {
            return !m_stop_token.stop_requested();
        }

      private:
        Tick m_tick;
        std::stop_token m_stop_token;
    };

    friend auto tag_invoke(unifex::tag_t<cpo::make_input_stream> _,
                           const AlwaysReady& term,
                           std::stop_token&& stop_token) -> InputStream
    {
        return {std::move(stop_token)};
    }
};

static_assert(concepts::scheduling_term<AlwaysReady>);

}  // namespace mrc::ops
