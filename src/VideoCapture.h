// VideoCapture.h
//
// Media Foundation-based H.264 video encoder for PrintScreen V4.
//
// GPU-agnostic: uses whichever hardware encoder MFT is available (NVENC on
// Nvidia, AMF on AMD, QuickSync on Intel) or falls back to software.
//
// Input: ID3D11Texture2D frames in BGRA8 (DXGI_FORMAT_B8G8R8A8_UNORM), which
//        matches Desktop Duplication output.
// Output: MP4 container with H.264 video, written via IMFSinkWriter.
//
// Threading: all public methods must be called from the same thread (typically
// your existing async worker thread). The supplied D3D11 device MUST have
// multithread protection enabled -- Initialize() will enforce this.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

namespace Printscreen {

enum class EncoderPreference : std::uint8_t {
    Auto           = 0,  // Let MF choose: hardware if available, else software
    PreferHardware = 1,  // Require hardware; fail Initialize if none
    ForceSoftware  = 2   // Disable hardware MFTs entirely
};

enum class RateControlMode : std::uint8_t {
    CBR = 0,  // Constant bitrate
    VBR = 1,  // Variable bitrate (default)
    CQP = 2   // Constant quantization / quality
};

enum class VideoContainer : std::uint8_t {
    MP4 = 0,
    MKV = 1   // NOTE: not implemented in first pass; falls back to MP4 with log warning
};

struct VideoCaptureConfig {
    // Frame geometry. Must match the dimensions of the textures passed to
    // EncodeFrame(). For downscaling, do it in your capture pipeline before
    // handing the texture over.
    std::uint32_t width  = 1920;
    std::uint32_t height = 1080;

    // Target frame rate (frames per second). The encoder assumes a constant
    // frame rate; frame timestamps are derived from frameIndex * (1/fps).
    std::uint32_t frameRate = 60;

    // Bitrate in kilobits per second. Used for CBR/VBR. Ignored for CQP.
    std::uint32_t bitrateKbps = 16000;

    // Quality value 1-100, used only when rateControl == CQP.
    std::uint32_t qualityCqp = 70;

    // Keyframe (IDR) interval in seconds. 2 is a sensible default.
    std::uint32_t keyframeIntervalSec = 2;

    EncoderPreference encoderPreference = EncoderPreference::Auto;
    RateControlMode   rateControl       = RateControlMode::VBR;
    VideoContainer    container         = VideoContainer::MP4;

    // Absolute output path including extension.
    std::filesystem::path outputPath;
};

class VideoCapture {
public:
    VideoCapture() noexcept;
    ~VideoCapture();

    VideoCapture(const VideoCapture&)            = delete;
    VideoCapture& operator=(const VideoCapture&) = delete;
    VideoCapture(VideoCapture&&)                 = delete;
    VideoCapture& operator=(VideoCapture&&)      = delete;

    // Initialize Media Foundation, create the sink writer, and prepare the
    // encoder. Pass the same ID3D11Device used for Desktop Duplication so
    // encoded textures can be consumed without a cross-device copy.
    //
    // Returns true on success. On failure, the object is left in an
    // uninitialized state and the output file (if created) is removed.
    [[nodiscard]] bool Initialize(ID3D11Device* d3dDevice, const VideoCaptureConfig& config);

    // Encode one frame. frameIndex is zero-based and drives the presentation
    // timestamp; the caller is responsible for incrementing it monotonically.
    //
    // The texture must be width x height with format BGRA8. It does NOT need
    // to be kept alive after this call returns -- MF copies or references as
    // needed internally.
    //
    // Use the overload with sampleTimeHns for wall-clock timing (recommended for
    // video capture to avoid stutter from frame timing drift).
    [[nodiscard]] bool EncodeFrame(ID3D11Texture2D* frameTexture, std::uint64_t frameIndex);
    [[nodiscard]] bool EncodeFrame(ID3D11Texture2D* frameTexture, std::uint64_t frameIndex, LONGLONG sampleTimeHns, LONGLONG sampleDurationHns);

    // Flush and close the file cleanly. Must be called on normal completion
    // for the output to be playable.
    [[nodiscard]] bool Finalize();

    // Abort without finalizing. For MP4, the output will likely be
    // unplayable (no moov atom written). For MKV, it may be recoverable.
    // Safe to call from the cooperative-cancellation path.
    void Abort();

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    bool StartupMediaFoundation();
    bool CreateDeviceManager(ID3D11Device* d3dDevice);
    bool EnsureMultithreadProtected(ID3D11Device* d3dDevice);
    bool BuildOutputMediaType(Microsoft::WRL::ComPtr<IMFMediaType>& outType) const;
    bool BuildInputMediaType(Microsoft::WRL::ComPtr<IMFMediaType>& inType) const;
    bool BuildEncoderConfigAttributes(Microsoft::WRL::ComPtr<IMFAttributes>& attrs) const;
    bool CreateSinkWriter();
    void CleanupPartialFile() noexcept;
    void ReleaseAll() noexcept;

    Microsoft::WRL::ComPtr<IMFSinkWriter>        m_sinkWriter;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_deviceManager;
    Microsoft::WRL::ComPtr<ID3D11Device>         m_d3dDevice;

    VideoCaptureConfig m_config{};
    DWORD              m_videoStreamIndex       = 0;
    UINT               m_deviceManagerResetToken = 0;
    bool               m_mfStarted   = false;
    bool               m_initialized = false;
    bool               m_aborted     = false;
    std::atomic<bool>  m_finalized{ false };
    std::uint64_t      m_frameCount  = 0;    // frames successfully encoded
    bool               m_prevMultithreadState = false; // stored in Initialize, restored in ReleaseAll
};

} // namespace Printscreen
