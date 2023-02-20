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

#include "mrc/core/concepts/types.hpp"
#include "mrc/ops/component.hpp"
#include "mrc/ops/input_stream.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

namespace mrc::ops {

template <core::concepts::data T>
struct SchedulingTerm : public Component
{
    using data_type = T;
};

}  // namespace mrc::ops
