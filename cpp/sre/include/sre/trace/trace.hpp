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

// IWYU pragma: begin_exports
#include "sre/trace/context_stack.hpp"

#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>
// IWYU pragma: end_exports

namespace sre::trace {

using opentelemetry::trace::Scope;
using opentelemetry::trace::Span;
using opentelemetry::trace::Tracer;

template <typename T>
using Handle = opentelemetry::nostd::shared_ptr<T>;  // NOLINT todo(ryan) - update tidy

using TracerHandle = Handle<Tracer>;  // NOLINT
using SpanHandle   = Handle<Span>;    // NOLINT

/**
 * @brief Get the tracer object
 *
 * @return Handle<Tracer>
 */
Handle<Tracer> get_tracer();

}  // namespace sre::trace
