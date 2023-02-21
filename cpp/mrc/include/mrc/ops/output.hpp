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

#include "mrc/channel/v2/api.hpp"
#include "mrc/channel/v2/async_read.hpp"
#include "mrc/channel/v2/async_write.hpp"
#include "mrc/channel/v2/concepts/writable.hpp"
#include "mrc/coroutines/async_generator.hpp"
#include "mrc/coroutines/symmetric_transfer.hpp"
#include "mrc/coroutines/task.hpp"
#include "mrc/ops/component.hpp"
#include "mrc/ops/concepts/output_stream.hpp"
#include "mrc/ops/cpo/outputs.hpp"
#include "mrc/ops/edge.hpp"
#include "mrc/ops/forward.hpp"

#include <coroutine>
#include <utility>

namespace mrc::ops {

template <typename DataT>
struct Output final : public Component
{
  public:
    using data_type = DataT;

    explicit Output(std::size_t tag) :
      m_tag(tag),
      m_shared_state(std::make_shared<coroutines::SymmetricTransfer<DataT>>()),
      m_output_stream(m_shared_state)
    {}

    OutputStream<DataT> output_stream()
    {
        return m_output_stream;
    }

    bool is_connected() const
    {
        return !m_shared_state;
    }

    [[nodiscard]] coroutines::Task<> start() final
    {
        co_await m_shared_state->wait_until_initialized();
        co_return;
    }

    [[nodiscard]] coroutines::Task<> finalize() final
    {
        m_shared_state->close();
        co_return;
    }

  private:
    std::size_t m_tag;
    std::shared_ptr<coroutines::SymmetricTransfer<DataT>> m_shared_state;
    OutputStream<DataT> m_output_stream;
};

}  // namespace mrc::ops
