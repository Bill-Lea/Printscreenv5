#pragma once
#include "IEncoder.h"
#include "capture/CaptureRequest.h"

// ============================================================
// StillEncoder
// Handles single-frame formats: PNG, JPEG, BMP, TIFF, DDS.
// EncodeSequence uses only the first frame.
// ============================================================
class StillEncoder final : public IEncoder {
public:
    explicit StillEncoder(const CaptureRequest& req);

    EncodeResult Encode(
        const DirectX::ScratchImage& frame,
        const std::wstring&          outputPath,
        CancellationToken::Ptr       token,
        const CaptureExif&           exif = {}) override;

    EncodeResult EncodeSequence(
        std::vector<DirectX::ScratchImage>& frames,
        const std::wstring&                 outputPath,
        float, int, int,
        CancellationToken::Ptr              token) override;

private:
    HRESULT SaveWIC(const DirectX::ScratchImage& img,
                    const std::wstring& path,
                    CancellationToken::Ptr token,
                    const CaptureExif&    exif);
    HRESULT SaveDDS(const DirectX::ScratchImage& img,
                    const std::wstring& path,
                    CancellationToken::Ptr token);

    ImageFormat format_;
    float       jpegQuality_;
    TiffMode    tiffMode_;
    DDSMode     ddsMode_;
    int         pngCompression_;  // PNG zlib level 0-9
};
