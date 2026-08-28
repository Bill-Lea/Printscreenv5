#pragma once
#include "IEncoder.h"
#include <vector>
#include <string>

// ============================================================
// GifEncoder
// Encodes a sequence of on-disk BMP frames into an animated GIF.
// EncodeSequence loads frames from memory (DirectX::ScratchImage).
// EncodeFromFiles loads frames from pre-saved BMP files on disk —
// preferred for long recordings to avoid RAM exhaustion.
// ============================================================
class GifEncoder final : public IEncoder {
public:
    GifEncoder() = default;

    // IEncoder — uses in-memory frames
    EncodeResult Encode(
        const DirectX::ScratchImage& frame,
        const std::wstring&          outputPath,
        CancellationToken::Ptr       token,
        const CaptureExif&           exif = {}) override;

    EncodeResult EncodeSequence(
        std::vector<DirectX::ScratchImage>& frames,
        const std::wstring&                 outputPath,
        float                               fps,
        int                                 loopCount,
        int                                 deltaMode,
        CancellationToken::Ptr              token) override;

    // Disk-based path used by CaptureSession for long recordings
    EncodeResult EncodeFromFiles(
        const std::vector<std::wstring>& framePaths,
        const std::wstring&              outputPath,
        float                            fps,
        int                              loopCount,
        int                              deltaMode,
        int                              optimize,
        CancellationToken::Ptr           token);
};
