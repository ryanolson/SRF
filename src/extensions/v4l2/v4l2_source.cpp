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

#include "extensions/v4l2/v4l2_source.hpp"

#include "srf/memory/memory_kind.hpp"
#include "srf/memory/resources/host/pinned_memory_resource.hpp"

#include <glog/logging.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace srf::ext::v4l2 {

#define GXF_LOG_ERROR(...)                    \
    {                                         \
        char tmp[256];                        \
        std::snprintf(tmp, 256, __VA_ARGS__); \
        LOG(FATAL) << tmp;                    \
    }

void V4L2Source::data_source(rxcpp::subscriber<data::Reusable<memory::buffer>>& s)
{
    // initialize
    initialize();

    // source loop
    while (s.is_subscribed())
    {
        // Dequeue the next available buffer.
        v4l2_buffer v4l2_buf = {0};
        v4l2_buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory      = V4L2_MEMORY_MMAP;
        if (ioctl(m_fd, VIDIOC_DQBUF, &v4l2_buf) < 0)
        {
            LOG(FATAL) << "Failed to dequeue buffer from " << m_video_device;
        }

        auto& buf = m_buffers[v4l2_buf.index];

        // Allocate and convert to an RGBA output buffer.
        auto rgba_buf = m_pool->await_item();
        yuyv_to_rgba(buf.data(), rgba_buf->data(), m_width, m_height);

        // Return (queue) the buffer.
        if (ioctl(m_fd, VIDIOC_QBUF, &v4l2_buf) < 0)
        {
            GXF_LOG_ERROR("Failed to queue buffer %d on %s", v4l2_buf.index, m_video_device.c_str());
        }

        s.on_next(std::move(rgba_buf));
    }

    // finalize
    finalize();

    // complete on subscriber
    s.on_completed();
}

void V4L2Source::initialize()
{
    m_fd = open(m_video_device.c_str(), O_RDWR);  // todo(ryan) - upgrade to O_ASYNC
    if (m_fd < 0)
    {
        LOG(FATAL) << "Failed to open device, OPEN";
    }

    // Ask the device if it can capture frames
    v4l2_capability capability;
    if (ioctl(m_fd, VIDIOC_QUERYCAP, &capability) < 0)
    {
        LOG(FATAL) << "Failed to get device capabilities, VIDIOC_QUERYCAP";
    }

    // Get and check the device capabilities
    v4l2_capability caps;
    if (ioctl(m_fd, VIDIOC_QUERYCAP, &caps) < 0)
    {
        LOG(FATAL) << m_video_device << " is not a v4l2 device.";
    }
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        LOG(FATAL) << m_video_device << " is not a video capture device";
    }
    if (!(caps.capabilities & V4L2_CAP_STREAMING))
    {
        LOG(FATAL) << m_video_device << " does not support streaming i/o";
    }

    // Set image format
    v4l2_format fmt;
    uint32_t pixel_format   = V4L2_PIX_FMT_YUYV;
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = m_width;
    fmt.fmt.pix.height      = m_height;
    fmt.fmt.pix.pixelformat = pixel_format;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        GXF_LOG_ERROR("Failed to set the image format on %s (%dx%d, format = %d)",
                      m_video_device.c_str(),
                      m_width,
                      m_height,
                      pixel_format);
    }
    if (fmt.fmt.pix.width != m_width || fmt.fmt.pix.height != m_height || fmt.fmt.pix.pixelformat != pixel_format)
    {
        GXF_LOG_ERROR("Format not supported by %s", m_video_device.c_str());
    }

    // Request buffers from the device
    v4l2_requestbuffers req = {0};
    req.count               = m_buffer_count;
    req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory              = V4L2_MEMORY_MMAP;
    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0)
    {
        GXF_LOG_ERROR("Could not request buffer from device, VIDIOC_REQBUFS");
    }

    // Retrieve and map the buffers.
    for (size_t i = 0; i < req.count; i++)
    {
        v4l2_buffer buf = {0};
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
        if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            GXF_LOG_ERROR("Failed to query buffer from %s", m_video_device.c_str());
        }

        void* ptr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (ptr == nullptr)
        {
            GXF_LOG_ERROR("Failed to map buffer provided by %s", m_video_device.c_str());
        }

        m_buffers.emplace_back(ptr, buf.length, srf::memory::memory_kind::host);
    }

    // we should register these buffers with cuda
    // then do the color space conversion with a direct access cuda kernel reading from pinned host and writing to
    // device, see https://github.com/dusty-nv/jetson-utils/blob/master/cuda/cudaYUV-YUYV.cu for the kernels

    // Queue all buffers.
    for (int i = 0; i < m_buffers.size(); i++)
    {
        v4l2_buffer buf = {0};
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
        if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0)
        {
            GXF_LOG_ERROR("Failed to queue buffer %d on %s", i, m_video_device.c_str());
        }
    }

    m_pool = data::ReusablePool<memory::buffer>::create(m_pool_capacity);
    // #113 to get the partition resources to grab or create a new mr based on provided arena mr
    // for this it doesn't matter, since these are pre-allocated buffer, but they will not be mapped to the nic
    auto mr = std::make_shared<memory::pinned_memory_resource>();
    CHECK_LT(m_rgba_buffer_count, m_pool->size());
    auto bytes = m_width * m_height * 4;
    for (int i = 0; i < m_rgba_buffer_count; i++)
    {
        m_pool->emplace(bytes, mr);
    }

    // Start streaming
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0)
    {
        GXF_LOG_ERROR("Could not start streaming, VIDIOC_STREAMON");
    }
}

// host color conversion - move to gpu
void V4L2Source::yuyv_to_rgba(const void* yuyv, void* rgba, size_t width, size_t height)
{
    auto r_convert = [](int y, int cr) {
        double r = y + (1.4065 * (cr - 128));
        return static_cast<unsigned int>(std::max(0, std::min(255, static_cast<int>(r))));
    };
    auto g_convert = [](int y, int cb, int cr) {
        double g = y - (0.3455 * (cb - 128)) - (0.7169 * (cr - 128));
        return static_cast<unsigned int>(std::max(0, std::min(255, static_cast<int>(g))));
    };
    auto b_convert = [](int y, int cb) {
        double b = y + (1.7790 * (cb - 128));
        return static_cast<unsigned int>(std::max(0, std::min(255, static_cast<int>(b))));
    };

    const auto* yuyv_buf = static_cast<const unsigned char*>(yuyv);
    auto* rgba_buf       = static_cast<unsigned char*>(rgba);

    for (unsigned int i = 0, j = 0; i < width * height * 4; i += 8, j += 4)
    {
        int cb = yuyv_buf[j + 1];
        int cr = yuyv_buf[j + 3];

        // First pixel
        int y           = yuyv_buf[j];
        rgba_buf[i]     = r_convert(y, cr);
        rgba_buf[i + 1] = g_convert(y, cb, cr);
        rgba_buf[i + 2] = b_convert(y, cb);
        rgba_buf[i + 3] = 1;

        // Second pixel
        y               = yuyv_buf[j + 2];
        rgba_buf[i + 4] = r_convert(y, cr);
        rgba_buf[i + 5] = g_convert(y, cb, cr);
        rgba_buf[i + 6] = b_convert(y, cb);
        rgba_buf[i + 7] = 1;
    }
}

}  // namespace srf::ext::v4l2
