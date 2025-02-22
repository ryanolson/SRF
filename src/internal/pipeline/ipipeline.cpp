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

#include <srf/internal/pipeline/ipipeline.hpp>

#include "internal/pipeline/pipeline.hpp"
#include "srf/internal/pipeline/ipipeline.hpp"
#include "srf/internal/segment/idefinition.hpp"

#include <glog/logging.h>

#include <utility>

namespace srf::internal::pipeline {

IPipeline::IPipeline() : m_impl(std::make_shared<Pipeline>()) {}
IPipeline::~IPipeline() = default;

void IPipeline::register_segment(std::shared_ptr<const segment::IDefinition> segment)
{
    CHECK(segment);
    add_segment(segment->m_impl);
}

void IPipeline::add_segment(std::shared_ptr<const segment::Definition> segment)
{
    CHECK(segment);
    CHECK(m_impl);
    m_impl->add_segment(std::move(segment));
}

}  // namespace srf::internal::pipeline
