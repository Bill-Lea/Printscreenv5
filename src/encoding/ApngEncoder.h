#pragma once
#include "IEncoder.h"
#include <vector>
#include <string>
#include <cstdint>

// ============================================================
// ApngEncoder
// Encodes a sequence of frames into an Animated PNG (APNG).
// Supports full-frame and delta (transparency-based) encoding.
// EncodeFromFiles takes pre-saved BMP files for memory efficiency.
// ============================================================
class ApngEncoder final : public IEncoder {
public:
    ApngEncoder() = default;

    // IEncoder — wraps first frame as single-frame APNG
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

    // Disk-based path used by CaptureSession
    EncodeResult EncodeFromFiles(
        const std::vector<std::wstring>& framePaths,
        const std::wstring&              outputPath,
        float                            fps,
        int                              loopCount,
        int                              deltaMode,
        int                              optimize,
        CancellationToken::Ptr           token);

private:
    // APNG binary chunk helpers
    static void WriteUint32BE(uint8_t* dst, uint32_t v);
    static uint32_t Crc32(const uint8_t* data, size_t len);
    static void WriteChunk(std::vector<uint8_t>& out,
                           const char* type,
                           const uint8_t* data, uint32_t len);
    static void PatchUint32BE(std::vector<uint8_t>& buf, size_t offset, uint32_t v);
};
