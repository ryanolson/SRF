/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "radar.hpp"

#include "mrc/core/context.hpp"
#include "mrc/core/executor.hpp"
#include "mrc/options/options.hpp"
#include "mrc/pipeline/pipeline.hpp"
#include "mrc/segment/builder.hpp"
#include "mrc/segment/definition.hpp"

#include <cuda/std/complex>
#include <gtest/gtest.h>

#include <chrono>

class TestRadar : public ::testing::Test
{};

TEST_F(TestRadar, HoloscanExample)
{
    // mrc options
    mrc::channel::set_default_channel_size(8);

    auto options = std::make_unique<mrc::Options>();
    options->topology().user_cpuset("3");

    // create executor
    mrc::Executor executor(std::move(options));

    // create pipeline object
    auto pipeline = mrc::pipeline::make_pipeline();

    // radar config
    RadarConfig radar_config = {.numPulses = 128, .numSamples = 9000, .waveformLength = 1000, .numChannels = 16};

    // runtime
    RuntimeOptions opts = {.num_streams = 4, .iterations = 1000};

    // single segment
    auto seg = mrc::segment::Definition::create("radar", [radar_config, &opts](mrc::segment::Builder& s) {
        auto pc   = s.construct_object<PulseCompressionOp>("pulse_compression", radar_config, opts);
        auto tpc  = s.construct_object<ThreePulseCancellerOp>("three_pulse_canceller", radar_config, opts);
        auto dop  = s.construct_object<DopplerOp>("doppler", radar_config);
        auto cfar = s.construct_object<CFAROp>("cfar", radar_config, opts);

        s.make_edge(pc, tpc);
        s.make_edge(tpc, dop);
        s.make_edge(dop, cfar);
    });

    // register segments with the pipeline
    pipeline->register_segment(seg);

    // register the pipeline with the executor
    executor.register_pipeline(std::move(pipeline));

    // start the pipeline and wait until it finishes
    cudaDeviceSynchronize();
    std::cout << "mrc pipeline starting..." << std::endl;
    auto start = std::chrono::system_clock::now();
    executor.start();
    executor.join();
    cudaDeviceSynchronize();
    auto end = std::chrono::system_clock::now();
    LOG(INFO) << "total time: " << std::chrono::duration<float>(end - start).count();
    LOG(INFO) << "pipeline time: " << std::chrono::duration<float>(opts.m_finish - opts.m_start).count();
}
