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

#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/utils/macros.hpp"

#include <coroutine>

namespace mrc::ops {

template <typename T>
class OutputStream
{
  public:
    using data_type = T;

    explicit OutputStream(std::shared_ptr<coroutines::SymmetricTransfer<T>> shared_state) :
      m_shared_state(std::move(shared_state))
    {
        CHECK(m_shared_state);
    }

    ~OutputStream()
    {
        if (m_shared_state)
        {
            m_shared_state->close();
        }
    }

    DELETE_COPYABILITY(OutputStream);
    DEFAULT_MOVEABILITY(OutputStream);

    [[nodiscard]] auto emit(T& data) noexcept
    {
        return m_shared_state->async_write(data);
    }

    [[nodiscard]] auto emit(T&& data) noexcept
    {
        return m_shared_state->async_write(std::move(data));
    }

    [[nodiscard]] auto init()
    {
        return m_shared_state->wait_until_initialized();
    }

  private:
    std::shared_ptr<coroutines::SymmetricTransfer<T>> m_shared_state;
};

template <typename... Types>  // NOLINT
struct OutputStreams : private std::tuple<OutputStream<Types>...>
{
    using data_type = std::tuple<Types...>;

    template <std::size_t Id>
    auto& get_output()
    {
        return std::get<Id>(*this);
    }
};

}  // namespace mrc::ops
