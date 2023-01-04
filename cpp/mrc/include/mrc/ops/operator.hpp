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

#include "mrc/ops/concepts/connectable.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/schedulable.hpp"

namespace mrc::ops {

template <concepts::operable OperationT, concepts::schedulable SchedulingT>
requires std::same_as<typename OperationT::input_type, typename SchedulingT::data_type>
class Operator
{
  public:
    typename SchedulingT::data_type& input()
    requires concepts::input_connectable<SchedulingT>
    {
        return m_scheduling_term;
    }

  private:
    OperationT m_operation;
    SchedulingT m_scheduling_term;
};

}  // namespace mrc::ops
