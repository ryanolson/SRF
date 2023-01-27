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

#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/ops/concepts/input_stream.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/concepts/output_stream.hpp"

#include <unifex/tag_invoke.hpp>

#include <utility>

namespace mrc::ops::cpo {

// NOLINTBEGIN(readability-identifier-naming)

inline constexpr struct execute_cpo
{
    template <typename OperationT,
              concepts::input_stream_of<typename OperationT::input_data_type> InputT,
              concepts::output_stream_of<typename OperationT::output_data_type> OutputT>
    requires unifex::tag_invocable<execute_cpo, OperationT&, InputT&, OutputT&> and
             std::same_as<unifex::tag_invoke_result_t<execute_cpo, OperationT&, InputT&, OutputT&>, coroutines::Task<>>
             [[nodiscard]] auto operator()(OperationT& op, InputT& input_stream, OutputT& output_stream) const
             noexcept(unifex::is_nothrow_tag_invocable_v<execute_cpo, OperationT&, InputT&, OutputT&>)
                 -> coroutines::Task<>
    {
        return unifex::tag_invoke(*this, op, input_stream, output_stream);
    }

    // template <concepts::source OperationT,
    //           concepts::input_stream_of<Tick> InputT,
    //           concepts::output_stream_of<typename OperationT::output_type::data_type> OutputT>
    // requires unifex::tag_invocable<execute_cpo, const OperationT&, InputT&, OutputT&> and
    //          std::same_as<unifex::tag_invoke_result_t<execute_cpo, const OperationT&, InputT&, OutputT&>,
    //                       coroutines::Task<>>
    //          [[nodiscard]] auto operator()(const OperationT& op, InputT& input_stream, OutputT& output_stream) const
    //          noexcept(unifex::is_nothrow_tag_invocable_v<execute_cpo, const OperationT&, InputT&, OutputT&>)
    //              -> coroutines::Task<>
    // {
    //     return unifex::tag_invoke(*this, op, input_stream, output_stream);
    // }

    // template <concepts::sink OperationT, concepts::input_stream_of<typename OperationT::input_type::data_type>
    // InputT> requires unifex::tag_invocable<execute_cpo, const OperationT&, InputT&> and
    //          std::same_as<unifex::tag_invoke_result_t<execute_cpo, const OperationT&, InputT&>, coroutines::Task<>>
    //          [[nodiscard]] auto operator()(const OperationT& op, InputT& input_stream) const
    //          noexcept(unifex::is_nothrow_tag_invocable_v<execute_cpo, const OperationT&, InputT&>) ->
    //          coroutines::Task<>
    // {
    //     return unifex::tag_invoke(*this, op, input_stream);
    // }

} execute;

// NOLINTEND(readability-identifier-naming)

}  // namespace mrc::ops::cpo
