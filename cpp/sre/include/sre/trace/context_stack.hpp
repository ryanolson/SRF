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

#include "sre/common/macros.hpp"

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>

namespace sre::trace {

using opentelemetry::context::Context;
using opentelemetry::context::Token;

class CoroutineRuntimeContextStorage;

class ContextStack final
{
  public:
    ContextStack();
    ContextStack(Context context);

    DELETE_COPYABILITY(ContextStack);
    DELETE_MOVEABILITY(ContextStack);

  private:
    Context get_current() noexcept;

    const Context& attach(const Context& context) noexcept;

    bool detach(Token& token) noexcept;

    std::deque<Context> m_stack;

    friend CoroutineRuntimeContextStorage;
};

}  // namespace sre::trace
