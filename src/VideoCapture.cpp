// VideoCapture.cpp

#include "VideoCapture.h"

#include <codecapi.h>
#include <mferror.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <system_error>

// Media Foundation — linked via CMakeLists.txt (target_link_libraries)

namespace Printscreen {

using Microsoft::WRL::ComPtr;

namespace {

// Frame presentation time in 100-nanosecond units (MF's native time unit).
// We compute (frameIndex * 10'000'000) / fps rather than the other order to
// minimize accumulated rounding error across long captures.
constexpr LONGLONG kHnsPerSecond = 10'000'000LL;

LONGLONG FrameTimeHns(std::uint64_t frameIndex, std::uint32_t fps) noexcept {
    return static_cast<LONGLONG>((frameIndex * static_cast<std::uint64_t>(kHnsPerSecond)) / fps);
}

LONGLONG FrameDurationHns(std::uint32_t fps) noexcept {
    return kHnsPerSecond / static_cast<LONGLONG>(fps);
}

UINT32 EncoderRateControlConstant(RateControlMode mode) noexcept {
    switch (mode) {
        case RateControlMode::CBR: return eAVEncCommonRateControlMode_CBR;
        case RateControlMode::VBR: return eAVEncCommonRateControlMode_UnconstrainedVBR;
        case RateControlMode::CQP: return eAVEncCommonRateControlMode_Quality;
    }
    return eAVEncCommonRateControlMode_UnconstrainedVBR;
}

} // namespace

VideoCapture::VideoCapture() noexcept = default;

VideoCapture::~VideoCapture() {
    if (m_initialized && !m_finalized.load()) {
        // Destructor without Finalize() means we're cleaning up an in-progress
        // capture -- treat as abort.
        Abort();
    }
    ReleaseAll();
}

bool VideoCapture::Initialize(ID3D11Device* d3dDevice, const VideoCaptureConfig& config) {
    if (m_initialized) {
        spdlog::error("[VideoCapture] Initialize called on already-initialized instance");
        return false;
    }
    if (d3dDevice == nullptr) {
        spdlog::error("[VideoCapture] Initialize called with null D3D11 device");
        return false;
    }
    if (config.outputPath.empty()) {
        spdlog::error("[VideoCapture] Initialize called with empty output path");
        return false;
    }
    if (config.width == 0 || config.height == 0 || config.frameRate == 0) {
        spdlog::error("[VideoCapture] Invalid dimensions/framerate: {}x{} @ {}fps",
                      config.width, config.height, config.frameRate);
        return false;
    }

    m_config = config;

    if (m_config.container == VideoContainer::MKV) {
        spdlog::warn("[VideoCapture] MKV container requested but not yet implemented; falling back to MP4");
        // Adjust extension if caller supplied .mkv
        if (m_config.outputPath.extension() == ".mkv") {
            m_config.outputPath.replace_extension(".mp4");
        }
    }

    m_d3dDevice = d3dDevice;

    if (!EnsureMultithreadProtected(d3dDevice)) {
        spdlog::error("[VideoCapture] Failed to enable D3D11 multithread protection");
        ReleaseAll();
        return false;
    }

    if (!StartupMediaFoundation()) {
        ReleaseAll();
        return false;
    }

    if (!CreateDeviceManager(d3dDevice)) {
        ReleaseAll();
        return false;
    }

    if (!CreateSinkWriter()) {
        CleanupPartialFile();
        ReleaseAll();
        return false;
    }

    m_initialized = true;
    m_aborted     = false;
    m_finalized.store(false);

    spdlog::info("[VideoCapture] Initialized: {}x{} @ {}fps, {} kbps, -> {}",
                 m_config.width, m_config.height, m_config.frameRate,
                 m_config.bitrateKbps, m_config.outputPath.string());
    return true;
}

bool VideoCapture::EnsureMultithreadProtected(ID3D11Device* d3dDevice) {
    ComPtr<ID3D10Multithread> multithread;
    const HRESULT hr = d3dDevice->QueryInterface(IID_PPV_ARGS(&multithread));
    if (FAILED(hr) || !multithread) {
        spdlog::error("[VideoCapture] ID3D10Multithread not available (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }
    m_prevMultithreadState = multithread->GetMultithreadProtected();
    multithread->SetMultithreadProtected(TRUE);
    return true;
}

bool VideoCapture::StartupMediaFoundation() {
    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] MFStartup failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }
    m_mfStarted = true;
    return true;
}

bool VideoCapture::CreateDeviceManager(ID3D11Device* d3dDevice) {
    HRESULT hr = MFCreateDXGIDeviceManager(&m_deviceManagerResetToken, &m_deviceManager);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] MFCreateDXGIDeviceManager failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }
    hr = m_deviceManager->ResetDevice(d3dDevice, m_deviceManagerResetToken);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] IMFDXGIDeviceManager::ResetDevice failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

bool VideoCapture::BuildOutputMediaType(ComPtr<IMFMediaType>& outType) const {
    HRESULT hr = MFCreateMediaType(&outType);
    if (FAILED(hr)) return false;

    hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);                if (FAILED(hr)) return false;
    hr = outType->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_H264);               if (FAILED(hr)) return false;
    hr = outType->SetUINT32(MF_MT_AVG_BITRATE, m_config.bitrateKbps * 1000);   if (FAILED(hr)) return false;
    hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); if (FAILED(hr)) return false;
    hr = MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, m_config.width, m_config.height); if (FAILED(hr)) return false;
    hr = MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, m_config.frameRate, 1);          if (FAILED(hr)) return false;
    hr = MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);                   if (FAILED(hr)) return false;

    // Main profile is widely supported and gives better compression than Baseline.
    // Change to eAVEncH264VProfile_Base if you hit compatibility issues.
    hr = outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
    if (FAILED(hr)) return false;

    return true;
}

bool VideoCapture::BuildInputMediaType(ComPtr<IMFMediaType>& inType) const {
    HRESULT hr = MFCreateMediaType(&inType);
    if (FAILED(hr)) return false;

    // Desktop Duplication gives us BGRA8 -- MFVideoFormat_ARGB32 is the MF
    // subtype for 32-bit BGRA memory layout. The sink writer will insert a
    // color-conversion MFT (typically GPU-accelerated) to produce NV12 for
    // the H.264 encoder. For tighter perf, pre-convert to NV12 in your own
    // shader and set subtype to MFVideoFormat_NV12 here.
    hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);                   if (FAILED(hr)) return false;
    hr = inType->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_ARGB32);                if (FAILED(hr)) return false;
    hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);  if (FAILED(hr)) return false;
    hr = MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, m_config.width, m_config.height); if (FAILED(hr)) return false;
    hr = MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, m_config.frameRate, 1);          if (FAILED(hr)) return false;
    hr = MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);                   if (FAILED(hr)) return false;

    return true;
}

bool VideoCapture::BuildEncoderConfigAttributes(ComPtr<IMFAttributes>& attrs) const {
    HRESULT hr = MFCreateAttributes(&attrs, 6);
    if (FAILED(hr)) return false;

    hr = attrs->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                          EncoderRateControlConstant(m_config.rateControl));
    if (FAILED(hr)) return false;

    if (m_config.rateControl == RateControlMode::CQP) {
        const UINT32 q = std::clamp<UINT32>(m_config.qualityCqp, 1, 100);
        hr = attrs->SetUINT32(CODECAPI_AVEncCommonQuality, q);
        if (FAILED(hr)) return false;
    } else {
        hr = attrs->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, m_config.bitrateKbps * 1000);
        if (FAILED(hr)) return false;
    }

    // Keyframe (GOP) size in frames = interval_seconds * fps.
    const UINT32 gop = std::max<UINT32>(1, m_config.keyframeIntervalSec * m_config.frameRate);
    hr = attrs->SetUINT32(CODECAPI_AVEncMPVGOPSize, gop);
    if (FAILED(hr)) return false;

    return true;
}

bool VideoCapture::CreateSinkWriter() {
    // Sink writer factory attributes -- these control the SINK WRITER itself,
    // not the encoder. Hardware transform enable lives here.
    ComPtr<IMFAttributes> factoryAttrs;
    HRESULT hr = MFCreateAttributes(&factoryAttrs, 4);
    if (FAILED(hr)) return false;

    hr = factoryAttrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, m_deviceManager.Get());
    if (FAILED(hr)) return false;

    const BOOL enableHw = (m_config.encoderPreference != EncoderPreference::ForceSoftware) ? TRUE : FALSE;
    hr = factoryAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, enableHw);
    if (FAILED(hr)) return false;

    hr = factoryAttrs->SetUINT32(MF_LOW_LATENCY, FALSE);  // Quality over latency for offline capture
    if (FAILED(hr)) return false;

    // Throttling must stay ENABLED (FALSE). The capture pipeline recycles a
    // fixed pool of D3D11 textures; each WriteSample hands MF a sample that
    // references one of those pool slots. With throttling disabled the sink
    // writer accepts samples without limit, so when the encoder falls behind
    // it can still be reading a slot the producer is already overwriting —
    // torn frames in the output. With throttling on, WriteSample blocks once
    // a small number of samples are in flight, which bounds how many pool
    // slots MF can reference at once (well under the pool size). The
    // FrameQueue already decouples acquisition from encoding, so this
    // backpressure only ever stalls the consumer thread, never capture pacing.
    hr = factoryAttrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, FALSE);
    if (FAILED(hr)) return false;

    hr = MFCreateSinkWriterFromURL(m_config.outputPath.wstring().c_str(),
                                   nullptr, factoryAttrs.Get(), &m_sinkWriter);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] MFCreateSinkWriterFromURL failed for '{}' (hr=0x{:08x})",
                      m_config.outputPath.string(), static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IMFMediaType> outType;
    if (!BuildOutputMediaType(outType)) {
        spdlog::error("[VideoCapture] Failed to build output media type");
        return false;
    }

    hr = m_sinkWriter->AddStream(outType.Get(), &m_videoStreamIndex);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] AddStream failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IMFMediaType> inType;
    if (!BuildInputMediaType(inType)) {
        spdlog::error("[VideoCapture] Failed to build input media type");
        return false;
    }

    ComPtr<IMFAttributes> encoderCfg;
    if (!BuildEncoderConfigAttributes(encoderCfg)) {
        spdlog::error("[VideoCapture] Failed to build encoder config attributes");
        return false;
    }

    hr = m_sinkWriter->SetInputMediaType(m_videoStreamIndex, inType.Get(), encoderCfg.Get());
    if (FAILED(hr)) {
        // Common failure: PreferHardware requested but no HW encoder, or an
        // input format the encoder refuses. Log hr and suggest fallback.
        spdlog::error("[VideoCapture] SetInputMediaType failed (hr=0x{:08x}). "
                      "If encoderPreference=PreferHardware, try Auto or ForceSoftware.",
                      static_cast<unsigned>(hr));
        return false;
    }

    hr = m_sinkWriter->BeginWriting();
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] BeginWriting failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool VideoCapture::EncodeFrame(ID3D11Texture2D* frameTexture, std::uint64_t frameIndex) {
    return EncodeFrame(frameTexture, frameIndex,
                       FrameTimeHns(frameIndex, m_config.frameRate),
                       FrameDurationHns(m_config.frameRate));
}

bool VideoCapture::EncodeFrame(ID3D11Texture2D* frameTexture, std::uint64_t frameIndex,
                               LONGLONG sampleTimeHns, LONGLONG sampleDurationHns) {
    if (!m_initialized) {
        spdlog::error("[VideoCapture] EncodeFrame called before Initialize");
        return false;
    }
    if (m_aborted) {
        return false;  // Silent: cancellation path already logged
    }
    if (frameTexture == nullptr) {
        spdlog::error("[VideoCapture] EncodeFrame called with null texture (frame {})", frameIndex);
        return false;
    }

    // Wrap the D3D11 texture in an IMFMediaBuffer. The buffer holds a
    // reference to the texture for its lifetime; once the sample is released
    // inside MF, the reference is dropped.
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),
                                           frameTexture, 0, FALSE, &buffer);
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] MFCreateDXGISurfaceBuffer failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    // MF requires a current length on the buffer. For 2D GPU buffers, we set
    // this from the IMF2DBuffer contiguous length.
    ComPtr<IMF2DBuffer> buf2d;
    DWORD contiguousLen = 0;
    if (SUCCEEDED(buffer.As(&buf2d))) {
        buf2d->GetContiguousLength(&contiguousLen);
    } else {
        // Fallback: compute from BGRA8 size.
        contiguousLen = m_config.width * m_config.height * 4;
    }
    hr = buffer->SetCurrentLength(contiguousLen);
    if (FAILED(hr)) return false;

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return false;

    hr = sample->AddBuffer(buffer.Get());                       if (FAILED(hr)) return false;
    hr = sample->SetSampleTime(sampleTimeHns);                                   if (FAILED(hr)) return false;
    hr = sample->SetSampleDuration(sampleDurationHns);                             if (FAILED(hr)) return false;

    hr = m_sinkWriter->WriteSample(m_videoStreamIndex, sample.Get());
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] WriteSample failed at frame {} (hr=0x{:08x})",
                      frameIndex, static_cast<unsigned>(hr));
        return false;
    }

    ++m_frameCount;
    return true;
}

bool VideoCapture::Finalize() {
    if (!m_initialized) {
        spdlog::warn("[VideoCapture] Finalize called on uninitialized instance");
        return false;
    }
    if (m_finalized.exchange(true)) {
        spdlog::warn("[VideoCapture] Finalize called more than once");
        return false;
    }
    if (m_aborted) {
        spdlog::info("[VideoCapture] Finalize skipped -- capture was aborted");
        return false;
    }

    const HRESULT hr = m_sinkWriter->Finalize();
    if (FAILED(hr)) {
        spdlog::error("[VideoCapture] SinkWriter::Finalize failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        CleanupPartialFile();
        ReleaseAll();
        return false;
    }

    spdlog::info("[VideoCapture] Finalized: {}", m_config.outputPath.string());
    ReleaseAll();
    return true;
}

void VideoCapture::Abort() {
    if (!m_initialized || m_aborted) return;

    m_aborted = true;
    spdlog::info("[VideoCapture] Abort requested -- {} frames encoded", m_frameCount);

    // If at least one frame was encoded, try to finalize for a playable MP4.
    // This is a best-effort: partial files may still be unplayable depending
    // on the encoder, but it's better than leaving a completely broken file.
    bool partialFinalizeOk = false;
    if (m_frameCount > 0 && m_sinkWriter) {
        spdlog::info("[VideoCapture] Attempting partial finalize for {} frames", m_frameCount);
        const HRESULT hr = m_sinkWriter->Finalize();
        if (FAILED(hr)) {
            spdlog::warn("[VideoCapture] Partial finalize failed (hr=0x{:08x}); file may be unplayable",
                         static_cast<unsigned>(hr));
        } else {
            spdlog::info("[VideoCapture] Partial finalize succeeded");
            partialFinalizeOk = true;
        }
    }

    m_finalized.store(true);  // sink writer is closed either way
    m_sinkWriter.Reset();

    // Only delete the output if the partial finalize did NOT succeed — the
    // previous code deleted the file unconditionally, destroying the very
    // partial MP4 it had just successfully finalized.
    if (!partialFinalizeOk) {
        CleanupPartialFile();
    }
    ReleaseAll();
}

void VideoCapture::CleanupPartialFile() noexcept {
    if (m_config.outputPath.empty()) return;
    std::error_code ec;
    if (std::filesystem::exists(m_config.outputPath, ec)) {
        std::filesystem::remove(m_config.outputPath, ec);
        if (ec) {
            spdlog::warn("[VideoCapture] Could not remove partial file '{}': {}",
                         m_config.outputPath.string(), ec.message());
        }
    }
}

void VideoCapture::ReleaseAll() noexcept {
    // Restore D3D11 multithread state to what it was before we changed it
    if (m_d3dDevice) {
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(m_d3dDevice->QueryInterface(IID_PPV_ARGS(&multithread)))) {
            multithread->SetMultithreadProtected(m_prevMultithreadState);
        }
    }

    m_sinkWriter.Reset();
    m_deviceManager.Reset();
    m_d3dDevice.Reset();

    if (m_mfStarted) {
        MFShutdown();
        m_mfStarted = false;
    }

    m_initialized = false;
    m_frameCount  = 0;
}

} // namespace Printscreen
