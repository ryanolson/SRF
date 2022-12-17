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
#pragma once

#include "mrc/data/reusable_pool.hpp"
#include "mrc/node/generic_node.hpp"
#include "mrc/node/generic_sink.hpp"
#include "mrc/node/generic_source.hpp"

#include <cuda/std/complex>
#include <gtest/gtest.h>
#include <matx.h>

#include <chrono>

using namespace matx;

using ftype       = float;
using ComplexType = cuda::std::complex<ftype>;

/* Structures for passing parameters between operators */
struct RuntimeOptions
{
    std::size_t num_streams{1};
    std::size_t iterations{100};
    mutable std::chrono::time_point<std::chrono::system_clock> m_start;
    mutable std::chrono::time_point<std::chrono::system_clock> m_finish;
};

struct CudaStream
{
    CudaStream();

    ~CudaStream();

    cudaStream_t stream;
};

class StreamProvider
{
  public:
    StreamProvider(mrc::data::Reusable<CudaStream> stream_handle);

    inline cudaStream_t stream()
    {
        return m_stream_handle->stream;
    }

    mrc::data::Reusable<CudaStream> release_stream();

  private:
    mrc::data::Reusable<CudaStream> m_stream_handle;
};

struct ThreePulseCancellerData : public StreamProvider
{
    ThreePulseCancellerData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _inputView,
                            mrc::data::Reusable<CudaStream> _stream);

    mrc::data::Reusable<tensor_t<ComplexType, 3>> inputView;
};

using ThreePulseCancellerHandle = std::unique_ptr<ThreePulseCancellerData>;

struct DopplerData : public StreamProvider
{
    DopplerData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _tpcView,
                tensor_t<ftype, 1>* _cancelMask,
                mrc::data::Reusable<CudaStream> _stream);
    mrc::data::Reusable<tensor_t<ComplexType, 3>> tpcView;
    tensor_t<ftype, 1>* cancelMask;
};

using DopplerHandle = std::unique_ptr<DopplerData>;

struct CFARData : public StreamProvider
{
    CFARData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _tpcView, mrc::data::Reusable<CudaStream> _stream);
    mrc::data::Reusable<tensor_t<ComplexType, 3>> tpcView;
};

using CFARHandle = std::unique_ptr<CFARData>;

struct CFARResults
{
    tensor_t<ftype, 3>* normT = nullptr;
    tensor_t<ftype, 3>* ba    = nullptr;
    tensor_t<int, 3>* dets    = nullptr;
    tensor_t<ftype, 3>* xPow  = nullptr;
};

/* Custom MatX operator for calculation detections in CFAR step. */
template <class O, class I1, class I2, class I3, class I4>
class calcDets : public BaseOp<calcDets<O, I1, I2, I3, I4>>
{
  private:
    O out_;
    I1 xpow_;
    I2 ba_;
    I3 norm_;
    I4 pfa_;

  public:
    calcDets(O out, I1 xpow, I2 ba, I3 norm, I4 pfa) : out_(out), xpow_(xpow), ba_(ba), norm_(norm), pfa_(pfa) {}

    __device__ inline void operator()(index_t idz, index_t idy, index_t idx)
    {
        typename I1::type xpow  = xpow_(idz, idy, idx);
        typename I2::type ba    = ba_(idz, idy, idx);
        typename I2::type norm  = norm_(idz, idy, idx);
        typename I2::type alpha = norm * (std::pow(pfa_, -1.0 / norm) - 1);
        out_(idz, idy, idx)     = (xpow > alpha * ba) ? 1 : 0;
    }

    __host__ __device__ inline index_t Size(uint32_t i) const
    {
        return out_.Size(i);
    }

    static inline constexpr __host__ __device__ int32_t Rank()
    {
        return O::Rank();
    }
};

struct RadarConfig
{
    std::int64_t numPulses;
    std::int64_t numSamples;
    std::int64_t waveformLength;
    std::int64_t numChannels;
};

class PulseCompressionOp final : public mrc::node::GenericSource<ThreePulseCancellerHandle>
{
  public:
    explicit PulseCompressionOp(RadarConfig radar_config, const RuntimeOptions& opts);

    void data_source(rxcpp::subscriber<source_type_t>& sub) final;

  private:
    const RadarConfig radar_config;

    index_t numSamplesRnd;

    std::size_t m_current_count{0UL};
    const RuntimeOptions& m_opts;

    tensor_t<ComplexType, 1>* waveformView = nullptr;
    // tensor_t<ComplexType, 3>* inputView    = nullptr;
    tensor_t<ftype, 0>* norms = nullptr;

    std::shared_ptr<mrc::data::ReusablePool<CudaStream>> m_stream_pool;
    std::shared_ptr<mrc::data::ReusablePool<tensor_t<ComplexType, 3>>> m_input_pool;
};

class ThreePulseCancellerOp final : public mrc::node::GenericNode<ThreePulseCancellerHandle, DopplerHandle>
{
  public:
    explicit ThreePulseCancellerOp(RadarConfig radar_config, const RuntimeOptions& opts);

  private:
    void on_data(sink_type_t&& tpc_data, rxcpp::subscriber<source_type_t>& subscriber) final;
    void on_completed(rxcpp::subscriber<source_type_t>& subscriber) final {}

    const RadarConfig radar_config;
    const RuntimeOptions& m_opts;

    index_t numCompressedSamples;
    index_t numPulsesRnd;

    tensor_t<ftype, 1>* cancelMask = nullptr;
    // tensor_t<ComplexType, 3>* tpcView = nullptr;
    std::shared_ptr<mrc::data::ReusablePool<tensor_t<ComplexType, 3>>> m_tpc_pool;
};

class DopplerOp : public mrc::node::GenericNode<DopplerHandle, CFARHandle>
{
  public:
    explicit DopplerOp(RadarConfig radar_config);

  private:
    void on_data(sink_type_t&& data, rxcpp::subscriber<source_type_t>& subscriber) final;

    const RadarConfig radar_config;
    index_t numCompressedSamples;
};

class CFAROp : public mrc::node::GenericSink<CFARHandle>
{
  public:
    explicit CFAROp(RadarConfig radar_config, const RuntimeOptions& opts);

  private:
    /**
     * @brief Stage 4 - Constant False Alarm Rate (CFAR) Detector - averaging or median
     *
     * filter CFAR detectors in general are designed to provide constant false
     * alarm rates by dynamically adjusting detection thresholds based on certain
     * statistical assumptions and interference estimates made from the data.
     * References:
     *   Richards, M. A., Scheer, J. A., Holm, W. A., "Principles of Modern Radar:
     *   Basic Principles",
     *       SciTech Publishing, Inc., 2010.  Section 16.4.
     *   Richards, M. A., "Fundamentals of Radar Signal Processing", McGraw-Hill,
     *   2005.
     *       Chapter 7. alpha below corresponds to equation (7.17)
     *   Also, http://en.wikipedia.org/wiki/Constant_false_alarm_rate

     * CFAR works by using a training window to average cells "near" a cell
     * under test (CUT) to estimate the background power for that cell. It is
     * an assumption that the average of the nearby cells represents a
     * reasonable background estimate. In general, there are guard cells (G)
     * and reference cells (R) around the CUT. The guard cells prevent
     * contributions of a potential target in the CUT from corrupting the
     * background estimate. More reference cells are preferred to better
     * estimate the background average. As implemented below, the CUT and
     * guard cells form a hole within the training window, but CA-CFAR is
     * largely just an averaging filter otherwise with a threshold check
     * at each pixel after applying the filter.
     * Currently, the window below is defined statically because it is then
     * easy to visualize, but more typically the number of guard and
     * reference cells would be given as input and the window would be
     * constructed; we could update to such an approach, but I'm keeping
     * it simple for now.

     * We apply CFAR to the power of X; X is still complex until this point
     * Xpow = abs(X).^2;
     */
    void on_data(sink_type_t&& cfar_data) final;
    void on_completed() final;

    const RadarConfig radar_config;
    const RuntimeOptions& m_opts;

    index_t numCompressedSamples;
    index_t numPulsesRnd;
    const index_t cfarMaskX          = 13;
    const index_t cfarMaskY          = 5;
    static const constexpr float pfa = 1e-5f;

    tensor_t<ftype, 3>* normT        = nullptr;
    tensor_t<ftype, 3>* ba           = nullptr;
    tensor_t<int, 3>* dets           = nullptr;
    tensor_t<ftype, 3>* xPow         = nullptr;
    tensor_t<ftype, 2>* cfarMaskView = nullptr;
};
