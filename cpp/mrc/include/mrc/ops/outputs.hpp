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

#include "mrc/ops/output.hpp"

namespace mrc::ops {

namespace detail {

template <typename T, typename = typename T::output_type>
class OutputsImpl;

// operators with a single output type and no concurrency method can be generator edges
// operators with multiple outputs can only be connected by channel edges
template <typename OperationT, typename... Types>  // NOLINT
class OutputsImpl<OperationT, std::tuple<Types...>> : public Component
{
    template <std::size_t... I>
    static std::tuple<Output<Types>...> make_outputs(std::index_sequence<I...> indexes)
    {
        return std::tuple<Output<Types>...>{Output<Types>(I)...};
    }

  public:
    using data_type = std::tuple<Types...>;

    OutputsImpl() : m_outputs(make_outputs(std::make_index_sequence<sizeof...(Types)>{})) {}

    constexpr std::uint32_t number_of_outputs() const noexcept
    {
        return sizeof...(Types);
    }

    [[nodiscard]] coroutines::Task<> initialize() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.initialize()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> start() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.start()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> stop() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.stop()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> complete() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.complete()), ...);
                },
                m_outputs);
        }
        co_return;
    }

    [[nodiscard]] coroutines::Task<> finalize() final
    {
        if constexpr (sizeof...(Types) > 0)
        {
            std::apply(
                [](auto&&... outputs) {
                    ((co_await outputs.finalize()), ...);
                },
                m_outputs);
        }
        co_return;
    }

  private:
    friend auto tag_invoke(unifex::tag_t<cpo::make_output_streams> _, OutputsImpl& self)
        -> std::tuple<OutputStream<Types>...>
    requires(sizeof...(Types) > 0)
    {
        return std::apply(
            [&](auto&&... args) {
                return std::make_tuple(args.output_stream()...);
            },
            self.m_outputs);
        ;
    }

    friend auto tag_invoke(unifex::tag_t<cpo::make_output_streams> _, OutputsImpl& self) -> std::tuple<>
    requires(sizeof...(Types) == 0)
    {
        return std::make_tuple();
    }

    std::tuple<Output<Types>...> m_outputs;
};

}  // namespace detail
template <typename T>
using Outputs = detail::OutputsImpl<T>;  // NOLINT

}  // namespace mrc::ops
