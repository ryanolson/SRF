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

#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"
#include "mrc/utils/macros.hpp"

#include <stop_token>

namespace mrc::ops {

template <typename T>
class InputStream
{
  public:
    using data_type = T;

    InputStream(coroutines::detail::AsyncGeneratorIterator<T>& iterator, std::stop_token stop_token) :
      m_iterator(iterator),
      m_stop_token(std::move(stop_token))
    {}

    operator bool() const noexcept
    {
        return m_iterator && !m_stop_token.stop_requested();
    }

    [[nodiscard]] auto next() noexcept -> decltype(auto)
    {
        return m_iterator.operator++();
    }

    auto data() noexcept -> T&
    {
        return *m_iterator;
    }

  private:
    std::stop_token m_stop_token;
    coroutines::detail::AsyncGeneratorIterator<T>& m_iterator;
};

// template <>
// class InputStream<Tick>
// {
//   public:
//     using data_type = Tick;

//     InputStream(coroutines::detail::AsyncGeneratorIterator<Tick> iterator, std::stop_token stop_token) :
//       m_iterator(std::move(iterator)),
//       m_stop_token(std::move(stop_token))
//     {}

//     operator bool() const noexcept
//     {
//         return m_iterator && !m_stop_token.stop_requested();
//     }

//     [[nodiscard]] auto next() noexcept -> decltype(auto)
//     {
//         return m_iterator.operator++();
//     }

//     const Tick& data() const noexcept
//     {
//         return *m_iterator;
//     }

//   private:
//     coroutines::detail::AsyncGeneratorIterator<Tick> m_iterator;
//     std::stop_token m_stop_token;
// };

}  // namespace mrc::ops
