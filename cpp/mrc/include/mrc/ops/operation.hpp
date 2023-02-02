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

#include "mrc/coroutines/task.hpp"
#include "mrc/ops/concepts/operable.hpp"
#include "mrc/ops/cpo/evaluate.hpp"
#include "mrc/ops/input_stream.hpp"
#include "mrc/ops/output_stream.hpp"
#include "mrc/ops/scheduling_terms/tick.hpp"

namespace mrc::ops {

namespace next {

struct OperationBase
{
    virtual coroutines::Task<> init()
    {
        co_return;
    }

    virtual coroutines::Task<> finalize()
    {
        co_return;
    }
};

template <typename OperationT, std::movable InputDataT, std::movable... OutputDataTs>  // NOLINT
class Operation
{
  public:
    using input_type  = InputDataT;
    using output_type = std::tuple<OutputDataTs...>;

  private:
    template <concepts::input_stream_of<input_type> InputStreamT, concepts::output_stream... OutputStreamsT>
    friend coroutines::Task<> tag_invoke(unifex::tag_t<cpo::execute> _,
                                         OperationT& op,
                                         InputStreamT& input_stream,
                                         std::tuple<OutputStreamsT...>& output_streams)
    {
        return op.execute(input_stream, output_streams);
    }
};

template <typename OperationT, std::movable InputDataT, std::movable OutputDataT>
class Operation<OperationT, InputDataT, OutputDataT> : public OperationBase
{
  public:
    using input_type  = InputDataT;
    using output_type = OutputDataT;

  private:
    template <concepts::input_stream_of<input_type> InputT, concepts::output_stream_of<output_type> OutputT>
    friend coroutines::Task<> tag_invoke(unifex::tag_t<cpo::execute> _,
                                         OperationT& op,
                                         InputT& input_stream,
                                         OutputT& output_stream)
    {
        return op.execute(input_stream, output_stream);
    }
};

template <typename OperationT, std::movable InputDataT>
class Operation<OperationT, InputDataT> : public OperationBase
{
  public:
    using input_type  = InputDataT;
    using output_type = void;

  private:
    template <concepts::input_stream_of<input_type> InputT>
    friend coroutines::Task<> tag_invoke(unifex::tag_t<cpo::execute> _, OperationT& op, InputT& input_stream)
    {
        return op.execute(input_stream);
    }
};

template <typename OperationT, std::movable OutputDataT>
using Source = Operation<OperationT, Tick, OutputDataT>;  // NOLINT

template <typename OperationT, std::movable InputDataT>
using Sink = Operation<OperationT, InputDataT>;  // NOLINT

}  // namespace next

namespace detail {
template <typename InputT, typename... OutputTs>  // NOLINT
struct OperationTypes;

// operation taking generic input_stream and output_stream
template <concepts::input_stream InputStreamT, concepts::output_stream OutputStreamT>
struct OperationTypes<InputStreamT, OutputStreamT>
{
    using input_type  = InputStreamT;
    using output_type = OutputStreamT;

    virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
};

// operation taking generic input type an output_stream type
template <typename InputT, concepts::output_stream OutputStreamT>
struct OperationTypes<InputT, OutputStreamT>
{
    using input_type  = InputStream<InputT>;
    using output_type = OutputStreamT;

    virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
};

// operation taking generic input_stream type and output type
template <concepts::input_stream InputStreamT, typename OutputT>
struct OperationTypes<InputStreamT, OutputT>
{
    using input_type  = InputStreamT;
    using output_type = OutputStream<OutputT>;

    virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
};

// operation taking generic input and output stream
template <typename InputT, typename OutputT>
struct OperationTypes<InputT, OutputT>
{
    using input_type  = InputStream<InputT>;
    using output_type = OutputStream<OutputT>;

    virtual coroutines::Task<> execute(InputStream<InputT>& input_stream, OutputStream<OutputT>& output_stream) = 0;
};

// sink of a generic output stream
template <concepts::input_stream InputStreamT>
struct OperationTypes<InputStreamT, void>
{
    using input_type  = InputStreamT;
    using output_type = void;

    virtual coroutines::Task<> execute(InputStreamT& input_stream) = 0;
};

// sink of a generic data type
template <typename InputT>
struct OperationTypes<InputT, void>
{
    using input_type  = InputStream<InputT>;
    using output_type = void;

    virtual coroutines::Task<> execute(InputStream<InputT>& input_stream) = 0;
};

}  // namespace detail

struct OperationBase
{
    virtual coroutines::Task<> initialize()
    {
        co_return;
    }

    virtual coroutines::Task<> finalize()
    {
        co_return;
    }
};

template <typename InputT, typename... OutputTs>  // NOLINT
struct Operation : public OperationBase, public detail::OperationTypes<InputT, OutputTs...>
{};

// template <typename InputT, typename... OutputTs>  // NOLINT
// struct Operation;

// // operation taking generic input_stream and output_stream
// template <concepts::input_stream InputStreamT, concepts::output_stream OutputStreamT>
// struct Operation<InputStreamT, OutputStreamT>
// {
//     using input_type  = InputStreamT;
//     using output_type = OutputStreamT;

//     virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
// };

// // operation taking generic input type an output_stream type
// template <typename InputT, concepts::output_stream OutputStreamT>
// struct Operation<InputT, OutputStreamT>
// {
//     using input_type  = InputStream<InputT>;
//     using output_type = OutputStreamT;

//     virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
// };

// // operation taking generic input_stream type and output type
// template <concepts::input_stream InputStreamT, typename OutputT>
// struct Operation<InputStreamT, OutputT>
// {
//     using input_type  = InputStreamT;
//     using output_type = OutputStream<OutputT>;

//     virtual coroutines::Task<> execute(input_type& input_stream, output_type& output_stream) = 0;
// };

// // operation taking generic input and output stream
// template <typename InputT, typename OutputT>
// struct Operation<InputT, OutputT>
// {
//     using input_type  = InputStream<InputT>;
//     using output_type = OutputStream<OutputT>;

//     virtual coroutines::Task<> execute(InputStream<InputT>& input_stream, OutputStream<OutputT>& output_stream) = 0;
// };

// // sink of a generic output stream
// template <concepts::input_stream InputStreamT>
// struct Operation<InputStreamT, void>
// {
//     using input_type  = InputStreamT;
//     using output_type = void;

//     virtual coroutines::Task<> execute(InputStreamT& input_stream) = 0;
// };

// // sink of a generic data type
// template <typename InputT>
// struct Operation<InputT, void>
// {
//     using input_type  = InputStream<InputT>;
//     using output_type = void;

//     virtual coroutines::Task<> execute(InputStream<InputT>& input_stream) = 0;
// };

template <typename OutputStreamT, concepts::input_stream_of<Tick> InputStreamT = InputStream<Tick>>
struct Source;

template <concepts::output_stream OutputStreamT, concepts::input_stream_of<Tick> InputStreamT>
struct Source<OutputStreamT, InputStreamT> : public Operation<InputStreamT, OutputStreamT>
{};

template <std::movable DataT, concepts::input_stream_of<Tick> InputStreamT>
struct Source<DataT, InputStreamT> : public Operation<InputStreamT, OutputStream<DataT>>
{};

template <typename InputStreamT>
struct Sink;

template <concepts::input_stream InputStreamT>
struct Sink<InputStreamT> : public Operation<InputStreamT, void>
{};

template <std::movable DataT>
struct Sink<DataT> : public Operation<InputStream<DataT>, void>
{};

// struct InitializeMixin
// {
//     // called once to setup any global state for the operation
//     // if the operation has a concurrency greater than 1, then
//     // any of the resources setup here must be thread safe
//     virtual Task<> setup()
//     {
//         co_return;
//     }

//     // called once after all operation tasks have been completed
//     // used to teardown the global state
//     virtual Task<> teardown()
//     {
//         co_return;
//     }
// };

// template <typename T, typename StateT>
// struct StatefulOperation : public Operation<T>
// {
//     using state_type = StateT;

//     virtual StateT make_state(std::size_t idx) = 0;
// };

}  // namespace mrc::ops
