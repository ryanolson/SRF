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

#include "mrc/channel/ingress.hpp"
#include "mrc/channel/status.hpp"
#include "mrc/channel/v2/writable_channel.hpp"
#include "mrc/core/expected.hpp"
#include "mrc/core/std26_tag_invoke.hpp"
#include "mrc/coroutines/concepts/awaitable.hpp"
#include "mrc/coroutines/event.hpp"
#include "mrc/coroutines/ring_buffer.hpp"

#include <unifex/any_unique.hpp>
#include <unifex/overload.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/this.hpp>

#include <concepts>
#include <coroutine>
#include <stdexcept>
#include <type_traits>

namespace mrc::runnable::v2 {

namespace concepts {

using namespace coroutines::concepts;

template <typename T>
concept value_provider = requires { typename T::value_type; };

template <typename T>
concept scheduling_type =
    requires(T t) {
        typename T::value_type;
        typename T::error_type;

        // explicit return_type
        requires std::same_as<typename T::return_type, mrc::expected<typename T::value_type, typename T::error_type>>;

        // T must be an awaitable with the expected return_type
        requires awaitable<T>;
        requires awaitable_of<T, typename T::return_type>;
    };

// clang-format off

template <typename T>
concept async_operation = requires(T t, typename T::value_type val) {
    typename T::value_type;
    { t.evaluate(std::move(val)) } -> awaiter;
};

// clang-format on

template <typename T>
concept async_operation_void = requires {
                                   requires async_operation<T>;
                                   requires std::same_as<typename T::value_type, void>;
                               };

template <typename T>
concept operation_type = requires { requires async_operation<T> || async_operation_void<T>; };

// clang-format off
// template<typename T>
// concept edge_writable = requires(T t, typename T::value_type data) {
//     typename T::value_type
//     { t.write(std::move(data)) } -> awaiter;
// };
// clang-format on

}  // namespace concepts

namespace edge {

namespace cpo {

inline constexpr struct write_cpo
{
    template <typename T, typename DataT>
    requires unifex::tag_invocable<write_cpo, const T&, DataT&&>
    auto operator()(const T& x, DataT&& data) const -> coroutines::Task<void>
    {
        return unifex::tag_invoke(*this, x, std::move(data));
    }
} write;

}  // namespace cpo

template <typename T>
class any_writable
  : public unifex::any_unique_t<unifex::overload<coroutines::Task<void>(const unifex::this_&, T&&)>(cpo::write)>
{
  private:
    using base_t =
        unifex::any_unique_t<unifex::overload<coroutines::Task<void>(const unifex::this_&, T&&)>(cpo::write)>;

  public:
    using value_type = T;

    // Inherit the constructors
    using base_t::base_t;
};

namespace concepts {

// clang-format off
template<typename T>
concept writable =
  requires(const T& t, typename T::value_type data)
{
    typename T::value_type;
    { edge::cpo::write(t, std::move(data)) } -> std::same_as<coroutines::Task<void>>;
    // { edge::cpo::write(t, std::move(data)) } -> std::same_as<void>;
};
// clang-format on

static_assert(concepts::writable<any_writable<int>>);

}  // namespace concepts

}  // namespace edge

// namespace cpo {

// inline constexpr struct scheduling_term_fn
// {
//     template <typename T>
//     requires concepts::scheduling_type<std::remove_reference<std26::tag_invoke_result_t<scheduling_term_fn, const
//     T&>>> constexpr auto operator()(const T& x)
//     {
//         return std26::tag_invoke(*this, x);
//     }
// } scheduling_term;

// }  // namespace cpo

template <typename ValueT, typename ErrorT>
struct SchedulingTerm
{
    using value_type  = ValueT;
    using error_type  = ErrorT;
    using return_type = mrc::expected<value_type, error_type>;
};

template <typename ValueT>
struct ComputeTerm
{
    using value_type = ValueT;
};

struct Runnable
{
    virtual ~Runnable() = default;
};

template <concepts::scheduling_type SchedulingT, concepts::operation_type OperationT>
requires std::same_as<typename SchedulingT::value_type, typename OperationT::value_type>
class Operator : public Runnable
{
  public:
  private:
    struct Context
    {
        coroutines::Event event_create;
        coroutines::Event event_setup;
        coroutines::Event event_start;
        coroutines::Event event_teardown;
        coroutines::Event event_destroy;

        SchedulingT scheduling_term;
        OperationT operation;
    };

    static coroutines::Task<void> main(std::unique_ptr<Context> context) {}

    static coroutines::Task<void> worker_task(SchedulingT& scheduling_term, OperationT& operation)
    {
        // update state: Running
        while (true)
        {
            auto data = co_await scheduling_term;

            if (!data)
            {
                break;
            }

            if constexpr (concepts::async_operation<OperationT>)
            {
                co_await operation.evaluate(std::move(*data));
            }
            else  // if constexpr (concepts::async_operation_void<OperationT>)
            {
                co_await operation.evalute();
            }
        }
        // update state: Completed
        co_return;
    }

    SchedulingT m_scheduling_term;
    OperationT m_operation;
    std::atomic<bool> m_started{false};
    std::mutex m_mutex;
};

template <typename... T>
class Outputs
{};

template <typename T>
class Output
{
  public:
    [[nodiscard]] auto async_write(T&& data) -> decltype(auto)
    {
        DCHECK(is_connected());
        return m_channel->async_write(std::move(data));
    }

    bool is_connected() const
    {
        return bool(m_channel);
    }

  private:
    std::shared_ptr<channel::v2::IWritableChannel<T>> m_channel;
};

template <typename T>
struct SingleOutput : public Output<T>
{
    using output_type = SingleOutput<T>;
};

template <typename... Types>
struct MultipleOutputs : private std::tuple<SingleOutput<Types>...>
{
    using output_type = MultipleOutputs<Types...>;

    template <std::size_t Id>
    auto& get_output()
    {
        return std::get<Id>(*this);
    }
};

struct MyOperation : public SingleOutput<int>
{
    using input_type = int;

    coroutines::Task<void> evaluate(input_type&& input)
    {
        co_await async_write(42);
        co_await async_write(2);
        co_return;
    }
};

struct MyOtherOperation : public MultipleOutputs<double, std::string>
{
    using input_type = int;

    coroutines::Task<void> evaluate(input_type&& input)
    {
        auto& double_out = get_output<0>();
        auto& string_out = get_output<1>();

        co_await double_out.async_write(3.14 * input);
        co_await string_out.async_write("hi mrc");

        co_return;
    }
};

}  // namespace mrc::runnable::v2
