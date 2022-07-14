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

#include "srf/data/reusable_pool.hpp"
#include "srf/memory/buffer.hpp"
#include "srf/memory/buffer_view.hpp"
#include "srf/node/generic_source.hpp"
#include "srf/runnable/thread_context.hpp"

namespace srf::ext::v4l2 {

// note: the final parameter enforces that the source is run on a thread runnable, not a fiber runnable
// until this is converted to an async runnable, it requires a dedicated thread

class V4L2Source final : public srf::node::GenericSource<data::Reusable<memory::buffer>, runnable::ThreadContext<>>
{
  public:
    V4L2Source(/* options */);

  private:
    void data_source(rxcpp::subscriber<data::Reusable<memory::buffer>>& t) final;

    void initialize();
    void finalize();

    static void yuyv_to_rgba(const void* yuyv, void* rgba, size_t width, size_t height);

    std::string m_video_device{"/dev/video0"};
    std::uint32_t m_width{640};
    std::uint32_t m_height{480};
    std::uint32_t m_buffer_count{2};
    std::size_t m_rgba_buffer_count{4};
    std::size_t m_pool_capacity{16};

    int m_fd;
    std::vector<srf::memory::buffer_view> m_buffers;
    std::shared_ptr<data::ReusablePool<memory::buffer>> m_pool;
};

}  // namespace srf::ext::v4l2
