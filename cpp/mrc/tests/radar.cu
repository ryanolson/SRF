
#include "radar.hpp"

#include "mrc/cuda/common.hpp"
#include "mrc/cuda/sync.hpp"

CudaStream::CudaStream()
{
    MRC_CHECK_CUDA(cudaStreamCreate(&stream));
    LOG(INFO) << "stream initialized";
}

CudaStream::~CudaStream()
{
    MRC_CHECK_CUDA(cudaStreamSynchronize(stream));
    MRC_CHECK_CUDA(cudaStreamDestroy(stream));
}

StreamProvider::StreamProvider(mrc::data::Reusable<CudaStream> stream_handle) :
  m_stream_handle(std::move(stream_handle))
{}

mrc::data::Reusable<CudaStream> StreamProvider::release_stream()
{
    return std::move(m_stream_handle);
}

ThreePulseCancellerData::ThreePulseCancellerData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _inputView,
                                                 mrc::data::Reusable<CudaStream> _stream) :
  inputView(std::move(_inputView)),
  StreamProvider(std::move(_stream))
{}

DopplerData::DopplerData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _tpcView,
                         tensor_t<ftype, 1>* _cancelMask,
                         mrc::data::Reusable<CudaStream> _stream) :
  tpcView(std::move(_tpcView)),
  cancelMask(_cancelMask),
  StreamProvider(std::move(_stream))
{}

CFARData::CFARData(mrc::data::Reusable<tensor_t<ComplexType, 3>> _tpcView, mrc::data::Reusable<CudaStream> _stream) :
  tpcView(std::move(_tpcView)),
  StreamProvider(std::move(_stream))
{}

PulseCompressionOp::PulseCompressionOp(RadarConfig radar_config, const RuntimeOptions& opts) :
  radar_config(std::move(radar_config)),
  m_opts(opts)
{
    LOG(INFO) << "pulse compress op constructor start";

    LOG(INFO) << "numCudaStreams: " << m_opts.num_streams;
    m_stream_pool = mrc::data::ReusablePool<CudaStream>::create(16);
    for (int i = 0; i < m_opts.num_streams; i++)
    {
        m_stream_pool->emplace();
    }
    auto stream_handle = m_stream_pool->await_item();

    norms = new tensor_t<ftype, 0>();

    LOG(INFO) << "numPulses: " << radar_config.numPulses;
    LOG(INFO) << "numSamples: " << radar_config.numSamples;
    LOG(INFO) << "numChannels: " << radar_config.numChannels;
    LOG(INFO) << "waveformLength: " << radar_config.waveformLength;

    numSamplesRnd = 1;
    while (numSamplesRnd < radar_config.numSamples)
    {
        numSamplesRnd *= 2;
    }
    LOG(INFO) << "numSamplesRnd: " << numSamplesRnd;

    waveformView = new tensor_t<ComplexType, 1>({numSamplesRnd});

    m_input_pool = mrc::data::ReusablePool<tensor_t<ComplexType, 3>>::create(16);
    for (int i = 0; i < m_opts.num_streams; i++)
    {
        auto view = std::unique_ptr<tensor_t<ComplexType, 3>>(
            new tensor_t<ComplexType, 3>({radar_config.numChannels, radar_config.numPulses, numSamplesRnd}));
        cudaMemset(view->Data(), 0, view->TotalSize() * sizeof(ComplexType));
        view->PrefetchDevice(stream_handle->stream);
        m_input_pool->add_item(std::move(view));
    }
    // inputView = new tensor_t<ComplexType, 3>({radar_config.numChannels, radar_config.numPulses, numSamplesRnd});

    cudaMemset(waveformView->Data(), 0, numSamplesRnd * sizeof(ComplexType));
    // cudaMemset(inputView->Data(), 0, inputView->TotalSize() * sizeof(ComplexType));

    waveformView->PrefetchDevice(stream_handle->stream);
    // inputView->PrefetchDevice(stream_handle->stream);

    MRC_CHECK_CUDA(cudaStreamSynchronize(stream_handle->stream));
    LOG(INFO) << "pulse compress op constructor finish";
    stream_handle.release();
}

void PulseCompressionOp::data_source(rxcpp::subscriber<source_type_t>& sub)
{
    auto& context = mrc::runnable::Context::get_runtime_context();

    m_opts.m_start = std::chrono::system_clock::now();

    while (sub.is_subscribed() && m_current_count < m_opts.iterations)
    {
        auto stream_handle = m_stream_pool->await_item();
        auto inputView     = m_input_pool->await_item();

        // Reshape waveform to be waveformLength
        auto waveformPart = waveformView->Slice({0}, {radar_config.waveformLength});
        auto waveformT =
            waveformView->template Clone<3>({radar_config.numChannels, radar_config.numPulses, matxKeepDim});
        auto waveformFull = waveformView->Slice({0}, {numSamplesRnd});

        auto x = *inputView;

        // create waveform (assuming waveform is the same for every pulse)
        // this allows us to precompute waveform in frequency domain
        // Apply a Hamming window to the waveform to suppress sidelobes. Other
        // windows could be used as well (e.g., Taylor windows). Ultimately, it is
        // just an element-wise weighting by a pre-computed window function.
        (waveformPart = waveformPart * hamming<0>({radar_config.waveformLength})).run(stream_handle->stream);

        // compute L2 norm
        sum(*norms, norm(waveformPart), stream_handle->stream);
        (*norms = sqrt(*norms)).run(stream_handle->stream);

        (waveformPart = waveformPart / *norms).run(stream_handle->stream);
        fft(waveformFull, waveformPart, 0, stream_handle->stream);
        (waveformFull = conj(waveformFull)).run(stream_handle->stream);

        fft(x, x, 0, stream_handle->stream);
        (x = x * waveformT).run(stream_handle->stream);
        ifft(x, x, 0, stream_handle->stream);

        sub.on_next(std::make_unique<ThreePulseCancellerData>(std::move(inputView), std::move(stream_handle)));
        context.yield();
        m_current_count++;
    }
}

ThreePulseCancellerOp::ThreePulseCancellerOp(RadarConfig radar_config, const RuntimeOptions& opts) :
  radar_config(std::move(radar_config)),
  m_opts(opts)
{
    numPulsesRnd = 1;
    while (numPulsesRnd <= radar_config.numPulses)
    {
        numPulsesRnd *= 2;
    }

    numCompressedSamples = radar_config.numSamples - radar_config.waveformLength + 1;

    m_tpc_pool = mrc::data::ReusablePool<tensor_t<ComplexType, 3>>::create(16);
    for (int i = 0; i < m_opts.num_streams; i++)
    {
        auto view = std::unique_ptr<tensor_t<ComplexType, 3>>(
            new tensor_t<ComplexType, 3>({radar_config.numChannels, numPulsesRnd, numCompressedSamples}));
        cudaMemset(view->Data(), 0, view->TotalSize() * sizeof(ComplexType));
        view->PrefetchDevice(0);
        m_tpc_pool->add_item(std::move(view));
    }

    // tpcView    = new tensor_t<ComplexType, 3>({radar_config.numChannels, numPulsesRnd, numCompressedSamples});
    cancelMask = new tensor_t<ftype, 1>({3});
    cancelMask->SetVals({1, -2, 1});

    // cudaMemset(tpcView->Data(), 0, tpcView->TotalSize() * sizeof(ComplexType));

    // tpcView->PrefetchDevice(0);
    cancelMask->PrefetchDevice(0);

    MRC_CHECK_CUDA(cudaDeviceSynchronize());
}

void ThreePulseCancellerOp::on_data(sink_type_t&& tpc_data, rxcpp::subscriber<source_type_t>& subscriber)
{
    auto tpcView = m_tpc_pool->await_item();

    auto x = tpc_data->inputView->Permute({0, 2, 1}).Slice(
        {0, 0, 0}, {radar_config.numChannels, numCompressedSamples, radar_config.numPulses});
    auto xo = tpcView->Permute({0, 2, 1}).Slice(
        {0, 0, 0}, {radar_config.numChannels, numCompressedSamples, radar_config.numPulses});
    conv1d(xo, x, *cancelMask, matxConvCorrMode_t::MATX_C_MODE_SAME, tpc_data->stream());

    subscriber.on_next(std::make_unique<DopplerData>(std::move(tpcView), cancelMask, tpc_data->release_stream()));
    tpc_data->inputView.release();
    mrc::runnable::Context::get_runtime_context().yield();
}
DopplerOp::DopplerOp(RadarConfig radar_config) : radar_config(std::move(radar_config))
{
    numCompressedSamples = radar_config.numSamples - radar_config.waveformLength + 1;
}
void DopplerOp::on_data(sink_type_t&& data, rxcpp::subscriber<source_type_t>& subscriber)
{
    auto dop_data = std::move(data);

    const index_t cpulses = radar_config.numPulses - (dop_data->cancelMask->Size(0) - 1);

    auto xc = dop_data->tpcView->Slice({0, 0, 0}, {radar_config.numChannels, cpulses, numCompressedSamples});
    auto xf = dop_data->tpcView->Permute({0, 2, 1});

    (xc = xc * hamming<1>({radar_config.numChannels,
                           radar_config.numPulses - (dop_data->cancelMask->Size(0) - 1),
                           numCompressedSamples}))
        .run(dop_data->stream());
    fft(xf, xf, 0, dop_data->stream());

    subscriber.on_next(std::make_unique<CFARData>(std::move(dop_data->tpcView), dop_data->release_stream()));
    mrc::runnable::Context::get_runtime_context().yield();
}
CFAROp::CFAROp(RadarConfig radar_config, const RuntimeOptions& opts) :
  radar_config(std::move(radar_config)),
  m_opts(opts)
{
    numPulsesRnd = 1;
    while (numPulsesRnd <= radar_config.numPulses)
    {
        numPulsesRnd *= 2;
    }

    numCompressedSamples = radar_config.numSamples - radar_config.waveformLength + 1;

    normT = new tensor_t<ftype, 3>(
        {radar_config.numChannels, numPulsesRnd + cfarMaskY - 1, numCompressedSamples + cfarMaskX - 1});
    ba = new tensor_t<ftype, 3>(
        {radar_config.numChannels, numPulsesRnd + cfarMaskY - 1, numCompressedSamples + cfarMaskX - 1});
    dets         = new tensor_t<int, 3>({radar_config.numChannels, numPulsesRnd, numCompressedSamples});
    xPow         = new tensor_t<ftype, 3>({radar_config.numChannels, numPulsesRnd, numCompressedSamples});
    cfarMaskView = new tensor_t<ftype, 2>({cfarMaskY, cfarMaskX});

    // Mask for cfar detection
    // G == guard, R == reference, C == CUT
    // mask = [
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R G G G R ;
    //    R G C G R ;
    //    R G G G R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ;
    //    R R R R R ];
    //  }
    cfarMaskView->SetVals({{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
                           {1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1},
                           {1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1},
                           {1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1},
                           {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}});

    // Pre-process CFAR convolution
    conv2d(*normT,
           ones({radar_config.numChannels, numPulsesRnd, numCompressedSamples}),
           *cfarMaskView,
           matxConvCorrMode_t::MATX_C_MODE_FULL,
           0);

    ba->PrefetchDevice(0);
    normT->PrefetchDevice(0);
    cfarMaskView->PrefetchDevice(0);
    dets->PrefetchDevice(0);
    xPow->PrefetchDevice(0);

    MRC_CHECK_CUDA(cudaDeviceSynchronize());
}
void CFAROp::on_data(sink_type_t&& cfar_data)
{
    (*xPow = norm(*cfar_data->tpcView)).run(cfar_data->stream());

    // Estimate the background average power in each cell
    // background_averages = conv2(Xpow, mask, 'same') ./ norm;
    conv2d(*ba, *xPow, *cfarMaskView, matxConvCorrMode_t::MATX_C_MODE_FULL, cfar_data->stream());

    // Computing number of cells contributing to each cell.
    // This can be done with a convolution of the cfarMask with
    // ones.
    // norm = conv2(ones(size(X)), mask, 'same');
    auto normTrim =
        normT->Slice({0, cfarMaskY / 2, cfarMaskX / 2},
                     {radar_config.numChannels, numPulsesRnd + cfarMaskY / 2, numCompressedSamples + cfarMaskX / 2});

    auto baTrim =
        ba->Slice({0, cfarMaskY / 2, cfarMaskX / 2},
                  {radar_config.numChannels, numPulsesRnd + cfarMaskY / 2, numCompressedSamples + cfarMaskX / 2});
    (baTrim = baTrim / normTrim).run(cfar_data->stream());

    // The scalar alpha is used as a multiplier on the background averages
    // to achieve a constant false alarm rate (under certain assumptions);
    // it is based upon the desired probability of false alarm (Pfa) and
    // number of reference cells used to estimate the background for the
    // CUT. For the purposes of computation, it can be assumed as a given
    // constant, although it does vary at the edges due to the different
    // training windows.
    // Declare a detection if the power exceeds the background estimate
    // times alpha for a particular cell.
    // dets(find(Xpow > alpha.*background_averages)) = 1;

    // These 2 branches are functionally equivalent.  A custom op is more
    // efficient as it can avoid repeated loads.
    calcDets(*dets, *xPow, baTrim, normTrim, pfa).run(cfar_data->stream());
    mrc::enqueue_stream_sync_event(cfar_data->stream()).get();
    cfar_data.reset();
    mrc::runnable::Context::get_runtime_context().yield();
}

void CFAROp::on_completed()
{
    m_opts.m_finish = std::chrono::system_clock::now();
}
