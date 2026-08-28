#pragma once
#include <string>
#include <vector>
#include "capture/CancellationToken.h"

namespace DirectX { class ScratchImage; }

// ============================================================
// EncodeResult — returned by every encoder
// ============================================================
struct EncodeResult {
    bool         success{false};
    std::string  error;
    std::wstring outputPath;

    static EncodeResult Ok(std::wstring path) {
        return {true, {}, std::move(path)};
    }
    static EncodeResult Fail(std::string msg) {
        return {false, std::move(msg), {}};
    }
    static EncodeResult Cancelled() {
        return {false, "Cancelled", {}};
    }
};

// ============================================================
// CaptureExif — optional EXIF metadata for still captures
// (defined here so IEncoder::Encode can accept it)
// ============================================================
struct CaptureExif {
    double focalLength35mm = 0.0;   // mm (35mm-equivalent)
    double fovDegrees      = 0.0;   // horizontal FOV in degrees
    int    imageWidth      = 0;     // pixels
    int    imageHeight     = 0;     // pixels
    std::string cameraMake;         // e.g. "Skyrim Engine"
    std::string cameraModel;        // e.g. "PrintScreen V4"
    std::string software;           // e.g. "PrintScreen V4"
};

// ============================================================
// IEncoder — abstract base for all image encoders
// ============================================================
class IEncoder {
public:
    virtual ~IEncoder() = default;

    // Single-frame encode.  Animated encoders may ignore this.
    // exif is optional; only StillEncoder uses it.
    virtual EncodeResult Encode(
        const DirectX::ScratchImage& frame,
        const std::wstring&          outputPath,
        CancellationToken::Ptr       token,
        const CaptureExif&           exif = {}) = 0;

    // Multi-frame encode.  Still encoders use only frames[0].
    virtual EncodeResult EncodeSequence(
        std::vector<DirectX::ScratchImage>& frames,
        const std::wstring&                 outputPath,
        float                               fps,
        int                                 loopCount,
        int                                 deltaMode,
        CancellationToken::Ptr              token) = 0;
};
