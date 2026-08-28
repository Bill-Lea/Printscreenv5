#include "PCH.h"
#include "ApngEncoder.h"
#include "stringutils.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <fstream>
#include <cstring>

// ============================================================
// CRC-32 and PNG binary helpers
// ============================================================

static uint32_t s_crcTable[256];
static bool     s_crcReady = false;

static void BuildCrcTable() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        s_crcTable[n] = c;
    }
    s_crcReady = true;
}

/*static*/ uint32_t ApngEncoder::Crc32(const uint8_t* data, size_t len) {
    if (!s_crcReady) BuildCrcTable();
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) c = s_crcTable[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

/*static*/ void ApngEncoder::WriteUint32BE(uint8_t* dst, uint32_t v) {
    dst[0] = (v >> 24) & 0xff;
    dst[1] = (v >> 16) & 0xff;
    dst[2] = (v >>  8) & 0xff;
    dst[3] =  v        & 0xff;
}

/*static*/ void ApngEncoder::WriteChunk(std::vector<uint8_t>& out,
                                         const char* type,
                                         const uint8_t* data, uint32_t len)
{
    uint8_t hdr[4]; WriteUint32BE(hdr, len);
    out.insert(out.end(), hdr, hdr + 4);
    size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    if (data && len > 0) out.insert(out.end(), data, data + len);
    uint32_t crc = Crc32(out.data() + crcStart, out.size() - crcStart);
    uint8_t crcBuf[4]; WriteUint32BE(crcBuf, crc);
    out.insert(out.end(), crcBuf, crcBuf + 4);
}

/*static*/ void ApngEncoder::PatchUint32BE(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    WriteUint32BE(buf.data() + offset, v);
}

// ============================================================
// RAII COM apartment guard.
//
// IMPORTANT ordering fix: the previous pattern used a `cleanup` lambda that
// called CoUninitialize() inside the return expression — i.e. while the WIC
// factory ComPtr (and any loop-scope WIC objects) were still alive. Those
// objects were then Release()d AFTER the apartment had been torn down, which
// is documented undefined behavior ("all interface pointers become invalid"
// after CoUninitialize) and can crash the game mid-APNG-encode. A CTD there
// also orphans the temp frame directory, because no destructor
// (TempFileGuard in RunAnimated) ever runs.
//
// Declare an instance of this guard BEFORE any ComPtr in the scope: locals
// are destroyed in reverse order, so CoUninitialize now always runs after
// every COM object has been released.
// ============================================================
namespace {
struct ComApartmentGuard {
    HRESULT hr;
    bool    owned;
    ComApartmentGuard() noexcept {
        hr    = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned = (hr == S_OK || hr == S_FALSE);
    }
    ~ComApartmentGuard() { if (owned) CoUninitialize(); }
    ComApartmentGuard(const ComApartmentGuard&)            = delete;
    ComApartmentGuard& operator=(const ComApartmentGuard&) = delete;
    // COM is usable if we initialized it, or the thread already runs a
    // different apartment mode (RPC_E_CHANGED_MODE — someone else owns it).
    bool Usable() const noexcept { return owned || hr == RPC_E_CHANGED_MODE; }
};
} // namespace

// ============================================================
// Internal: encode a ScratchImage to PNG bytes in memory
// ============================================================

static HRESULT EncodeImageToPNG(
    IWICImagingFactory* wic,
    const DirectX::Image* img,
    bool withAlpha,
    std::vector<uint8_t>& pngOut)
{
    Microsoft::WRL::ComPtr<IStream> mem;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem))) return E_FAIL;

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(mem.Get(), WICBitmapEncoderNoCache);

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));

    const WICPixelFormatGUID srcFmt = (img->format == DXGI_FORMAT_R8G8B8A8_UNORM)
        ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;
    WICPixelFormatGUID outFmt = withAlpha
        ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&outFmt);

    Microsoft::WRL::ComPtr<IWICBitmap> bmp;
    wic->CreateBitmapFromMemory(
        static_cast<UINT>(img->width), static_cast<UINT>(img->height),
        srcFmt, static_cast<UINT>(img->rowPitch),
        static_cast<UINT>(img->slicePitch), img->pixels, &bmp);

    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();

    STATSTG st{}; mem->Stat(&st, STATFLAG_NONAME);
    LARGE_INTEGER zero{};
    mem->Seek(zero, STREAM_SEEK_SET, nullptr);
    pngOut.resize(static_cast<size_t>(st.cbSize.QuadPart));
    ULONG read = 0;
    return mem->Read(pngOut.data(), static_cast<ULONG>(pngOut.size()), &read);
}

// Compute bounding box of changed pixels between two BGRA frames.
static bool DiffRect(const uint8_t* prev, size_t prevPitch,
                     const uint8_t* curr, size_t currPitch,
                     uint32_t W, uint32_t H,
                     uint32_t& outL, uint32_t& outT,
                     uint32_t& outR, uint32_t& outB,
                     uint32_t threshold = 2)
{
    outL = W; outT = H; outR = 0; outB = 0;
    bool any = false;
    for (uint32_t y = 0; y < H; ++y) {
        const uint8_t* pr = prev + y * prevPitch;
        const uint8_t* cr = curr + y * currPitch;
        for (uint32_t x = 0; x < W; ++x) {
            const uint8_t* p = pr + x * 4;
            const uint8_t* c = cr + x * 4;
            bool changed = false;
            for (int ch = 0; ch < 3; ++ch)
                if (static_cast<uint32_t>(std::abs(int(p[ch]) - int(c[ch]))) > threshold)
                    { changed = true; break; }
            if (changed) {
                any = true;
                if (x < outL) outL = x; if (x > outR) outR = x;
                if (y < outT) outT = y; if (y > outB) outB = y;
            }
        }
    }
    return any;
}

// Extract sub-region with optional transparency delta encoding.
static void ExtractRegion(
    const uint8_t* prev, size_t prevPitch,
    const uint8_t* curr, size_t currPitch,
    uint32_t left, uint32_t top, uint32_t rW, uint32_t rH,
    bool delta, uint32_t threshold,
    std::vector<uint8_t>& outPx, size_t& outPitch)
{
    outPitch = rW * 4;
    outPx.resize(rH * outPitch);
    for (uint32_t dy = 0; dy < rH; ++dy) {
        const uint8_t* pr = prev + (top + dy) * prevPitch;
        const uint8_t* cr = curr + (top + dy) * currPitch;
        uint8_t*       dst = outPx.data() + dy * outPitch;
        for (uint32_t dx = 0; dx < rW; ++dx) {
            const uint8_t* p = pr + (left + dx) * 4;
            const uint8_t* c = cr + (left + dx) * 4;
            uint8_t* d = dst + dx * 4;
            if (delta) {
                bool changed = false;
                for (int ch = 0; ch < 3; ++ch)
                    if (static_cast<uint32_t>(std::abs(int(p[ch]) - int(c[ch]))) > threshold)
                        { changed = true; break; }
                d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
                d[3] = changed ? 255 : 0;
            } else {
                d[0] = c[0]; d[1] = c[1]; d[2] = c[2]; d[3] = c[3];
            }
        }
    }
}

// ============================================================
// IEncoder overrides
// ============================================================

EncodeResult ApngEncoder::Encode(
    const DirectX::ScratchImage& frame,
    const std::wstring& outputPath,
    CancellationToken::Ptr token,
    const CaptureExif&   exif)
{
    // exif is unused for APNG format — no metadata container.
    (void)exif;

    std::vector<DirectX::ScratchImage> frames;
    DirectX::ScratchImage copy;
    const DirectX::Image* img = frame.GetImage(0, 0, 0);
    if (!img) return EncodeResult::Fail("Null image");
    copy.Initialize2D(img->format, img->width, img->height, 1, 1);
    const DirectX::Image* dst = copy.GetImage(0, 0, 0);
    if (!dst) return EncodeResult::Fail("Copy alloc failed");
    std::memcpy(dst->pixels, img->pixels, img->slicePitch);
    frames.push_back(std::move(copy));
    return EncodeSequence(frames, outputPath, 1.0f, 1, 0, token);
}

EncodeResult ApngEncoder::EncodeSequence(
    std::vector<DirectX::ScratchImage>& frames,
    const std::wstring& outputPath,
    float fps, int loopCount, int deltaMode,
    CancellationToken::Ptr token)
{
    if (frames.empty()) return EncodeResult::Fail("No frames");

    // Declared BEFORE every ComPtr below — see ComApartmentGuard note above.
    ComApartmentGuard com;
    if (!com.Usable()) return EncodeResult::Fail("COM init failed");

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return EncodeResult::Fail("WIC factory failed");

    const double frameInterval = 1.0 / std::max(0.1, static_cast<double>(fps));
    const uint16_t delayNum = static_cast<uint16_t>(frameInterval * 1000);
    const uint16_t delayDen = 1000;

    const DirectX::Image* first = frames[0].GetImage(0, 0, 0);
    if (!first) return EncodeResult::Fail("First frame null");
    const uint32_t W = static_cast<uint32_t>(first->width);
    const uint32_t H = static_cast<uint32_t>(first->height);

    // Encode frame 0 as base PNG, then assemble APNG
    std::vector<uint8_t> basePNG;
    if (FAILED(EncodeImageToPNG(wic.Get(), first, false, basePNG)))
        return EncodeResult::Fail("Base frame PNG encode failed");

    // --- Parse base PNG and inject APNG chunks ---
    const uint8_t pngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    std::vector<uint8_t> out;
    out.insert(out.end(), pngSig, pngSig + 8);

    // acTL data
    uint8_t acTLData[8]{};
    WriteUint32BE(acTLData,     static_cast<uint32_t>(frames.size()));
    WriteUint32BE(acTLData + 4, loopCount == 0 ? 0u : static_cast<uint32_t>(loopCount));

    // Parse base PNG chunks and rebuild
    size_t pos = 8;
    bool ihdtWritten = false;
    uint32_t seqNum = 0;
    while (pos + 12 <= basePNG.size()) {
        uint32_t chunkLen = (static_cast<uint32_t>(basePNG[pos]) << 24)
                 | (static_cast<uint32_t>(basePNG[pos+1]) << 16)
                 | (static_cast<uint32_t>(basePNG[pos+2]) << 8)
                 |  static_cast<uint32_t>(basePNG[pos+3]);
        const char* chunkType = reinterpret_cast<const char*>(&basePNG[pos + 4]);
        const uint8_t* chunkData = &basePNG[pos + 8];

        if (std::strncmp(chunkType, "IHDR", 4) == 0) {
            WriteChunk(out, "IHDR", chunkData, chunkLen);
            WriteChunk(out, "acTL", acTLData, 8);
            // fcTL for frame 0
            uint8_t fcTL[26]{};
            WriteUint32BE(fcTL,      seqNum++);
            WriteUint32BE(fcTL + 4,  W);
            WriteUint32BE(fcTL + 8,  H);
            WriteUint32BE(fcTL + 12, 0); // x offset
            WriteUint32BE(fcTL + 16, 0); // y offset
            fcTL[20] = static_cast<uint8_t>(delayNum >> 8);
            fcTL[21] = static_cast<uint8_t>(delayNum & 0xFF);
            fcTL[22] = static_cast<uint8_t>(delayDen >> 8);
            fcTL[23] = static_cast<uint8_t>(delayDen & 0xFF);
            fcTL[24] = 0; // APNG_DISPOSE_OP_NONE
            fcTL[25] = 0; // APNG_BLEND_OP_SOURCE
            WriteChunk(out, "fcTL", fcTL, 26);
            ihdtWritten = true;
        } else if (std::strncmp(chunkType, "IDAT", 4) == 0) {
            // First frame IDAT goes in as-is (already covered by fcTL above)
            WriteChunk(out, "IDAT", chunkData, chunkLen);
        } else if (std::strncmp(chunkType, "IEND", 4) == 0) {
            break; // Append IEND last
        } else {
            // Preserve other chunks (PLTE, gAMA, etc.)
            WriteChunk(out, std::string(chunkType, 4).c_str(), chunkData, chunkLen);
        }
        pos += 12 + chunkLen;
    }

    // Encode subsequent frames as fdAT chunks
    for (size_t i = 1; i < frames.size(); ++i) {
        if (token) { try { token->ThrowIfCancelled("ApngEncoder frame"); }
                     catch (...) { return EncodeResult::Cancelled(); } }

        const DirectX::Image* img = frames[i].GetImage(0, 0, 0);
        if (!img) { logger::warn("ApngEncoder: null frame {}", i); continue; }

        uint32_t rL = 0, rT = 0, rR = W - 1, rB = H - 1;
        bool hasDiff = false;
        const DirectX::Image* prev = frames[i-1].GetImage(0, 0, 0);

        if (deltaMode >= 1 && prev) {
            hasDiff = DiffRect(prev->pixels, prev->rowPitch,
                               img->pixels,  img->rowPitch,
                               W, H, rL, rT, rR, rB);
            if (!hasDiff) { rL=0; rT=0; rR=W-1; rB=H-1; }
        }

        const uint32_t rW = rR - rL + 1;
        const uint32_t rH = rB - rT + 1;

        std::vector<uint8_t> regionPx; size_t regionPitch = 0;
        if (deltaMode >= 1 && prev && hasDiff) {
            // deltaMode 1 = region extraction (no transparency)
            // deltaMode 2 = true delta (unchanged pixels get alpha=0)
            bool trueDelta = (deltaMode == 2);
            ExtractRegion(prev->pixels, prev->rowPitch,
                          img->pixels,  img->rowPitch,
                          rL, rT, rW, rH,
                          trueDelta, 2, regionPx, regionPitch);
        }

        // Encode region to PNG
        std::vector<uint8_t> framePNG;
        if (!regionPx.empty()) {
            // Create a temporary ScratchImage from regionPx
            DirectX::ScratchImage regionImg;
            if (SUCCEEDED(regionImg.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, rW, rH, 1, 1))) {
                const DirectX::Image* ri = regionImg.GetImage(0, 0, 0);
                if (ri) {
                    for (uint32_t y = 0; y < rH; ++y)
                        std::memcpy(ri->pixels + y * ri->rowPitch,
                                    regionPx.data() + y * regionPitch,
                                    regionPitch);
                    EncodeImageToPNG(wic.Get(), ri, true, framePNG);
                }
            }
        } else {
            EncodeImageToPNG(wic.Get(), img, false, framePNG);
        }

        if (framePNG.empty()) { logger::warn("ApngEncoder: frame {} PNG encode failed", i); continue; }

        // fcTL for this frame
        uint8_t fcTL[26]{};
        WriteUint32BE(fcTL,      seqNum++);
        WriteUint32BE(fcTL + 4,  rW);
        WriteUint32BE(fcTL + 8,  rH);
        WriteUint32BE(fcTL + 12, rL);
        WriteUint32BE(fcTL + 16, rT);
        fcTL[20] = static_cast<uint8_t>(delayNum >> 8);
        fcTL[21] = static_cast<uint8_t>(delayNum & 0xFF);
        fcTL[22] = static_cast<uint8_t>(delayDen >> 8);
        fcTL[23] = static_cast<uint8_t>(delayDen & 0xFF);
        // Disposal must be APNG_DISPOSE_OP_NONE (0) for BOTH frame kinds.
        // The old code used DISPOSE_OP_BACKGROUND (1) for full frames, which
        // clears the canvas to transparent after the frame — so a subsequent
        // delta region (blend OVER) composited onto a blank canvas and the
        // animation corrupted whenever a static moment (no diff → full frame)
        // was followed by a delta frame.
        fcTL[24] = 0;                           // APNG_DISPOSE_OP_NONE
        fcTL[25] = (!regionPx.empty()) ? 1 : 0; // OVER (delta) or SOURCE (full)
        WriteChunk(out, "fcTL", fcTL, 26);

        // Extract IDAT data from framePNG and write as fdAT
        size_t fp = 8;
        while (fp + 12 <= framePNG.size()) {
            uint32_t fLen = (static_cast<uint32_t>(framePNG[fp]) << 24)
                          | (static_cast<uint32_t>(framePNG[fp+1]) << 16)
                          | (static_cast<uint32_t>(framePNG[fp+2]) << 8)
                          |  static_cast<uint32_t>(framePNG[fp+3]);
            const char* fType = reinterpret_cast<const char*>(&framePNG[fp + 4]);
            if (std::strncmp(fType, "IDAT", 4) == 0) {
                // Prepend sequence number and write as fdAT
                std::vector<uint8_t> fdatPayload;
                uint8_t seqBuf[4]; WriteUint32BE(seqBuf, seqNum++);
                fdatPayload.insert(fdatPayload.end(), seqBuf, seqBuf + 4);
                fdatPayload.insert(fdatPayload.end(), &framePNG[fp+8], &framePNG[fp+8+fLen]);
                WriteChunk(out, "fdAT", fdatPayload.data(), static_cast<uint32_t>(fdatPayload.size()));
            }
            fp += 12 + fLen;
            if (std::strncmp(fType, "IEND", 4) == 0) break;
        }
    }

    // Final IEND
    WriteChunk(out, "IEND", nullptr, 0);

    // Write file
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) return EncodeResult::Fail("Cannot write: " + util::wstring_to_utf8(outputPath));
    ofs.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    ofs.close();

    logger::info("ApngEncoder::EncodeSequence: wrote {} bytes to {}",
                 out.size(), util::wstring_to_utf8(outputPath));
    return EncodeResult::Ok(outputPath);
}

// ============================================================
// EncodeFromFiles — disk-based path (same algorithm but loads BMPs)
// ============================================================
EncodeResult ApngEncoder::EncodeFromFiles(
    const std::vector<std::wstring>& framePaths,
    const std::wstring& outputPath,
    float fps, int loopCount,
    int deltaMode, int /*optimize*/,
    CancellationToken::Ptr token)
{
    logger::info("ApngEncoder::EncodeFromFiles: {} frames", framePaths.size());
    if (framePaths.empty()) return EncodeResult::Fail("No frame files");

    // Load all frames then delegate to EncodeSequence
    std::vector<DirectX::ScratchImage> frames;
    frames.reserve(framePaths.size());
    for (const auto& path : framePaths) {
        if (token) { try { token->ThrowIfCancelled("ApngEncoder load frames"); }
                     catch (...) { return EncodeResult::Cancelled(); } }
        DirectX::ScratchImage img;
        if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, img))) {
            logger::warn("ApngEncoder: failed to load {}", util::wstring_to_utf8(path));
            continue;
        }
        frames.push_back(std::move(img));
    }

    if (frames.empty()) return EncodeResult::Fail("No frames loaded");
    return EncodeSequence(frames, outputPath, fps, loopCount, deltaMode, token);
}
