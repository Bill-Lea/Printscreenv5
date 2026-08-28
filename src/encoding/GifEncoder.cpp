#include "PCH.h"
#include "GifEncoder.h"
#include "stringutils.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <algorithm>

// ============================================================
// Internal helpers (file scope)
// ============================================================

namespace {

// RAII COM apartment guard.
//
// IMPORTANT ordering fix: the previous pattern used a `cleanup` lambda that
// called CoUninitialize() inside the return expression — i.e. while the WIC
// factory/stream/encoder/palette ComPtr locals were still alive. Those
// objects were then Release()d AFTER the apartment had been torn down, which
// is documented undefined behavior ("all interface pointers become invalid"
// after CoUninitialize) and can crash the game mid-AGIF-encode. A CTD there
// also orphans the temp frame directory, because no destructor
// (TempFileGuard in RunAnimated) ever runs.
//
// Declare an instance of this guard BEFORE any ComPtr in the scope: locals
// are destroyed in reverse order, so CoUninitialize now always runs after
// every COM object has been released.
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
// IEncoder overrides
// ============================================================

EncodeResult GifEncoder::Encode(
    const DirectX::ScratchImage& frame,
    const std::wstring& outputPath,
    CancellationToken::Ptr token,
    const CaptureExif&   exif)
{
    // exif is unused for GIF format — no metadata container.
    (void)exif;

    std::vector<DirectX::ScratchImage> frames;
    // ScratchImage is move-only; make a one-element sequence.
    // We encode at 1 fps single-loop.
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

EncodeResult GifEncoder::EncodeSequence(
    std::vector<DirectX::ScratchImage>& frames,
    const std::wstring& outputPath,
    float fps, int loopCount, int deltaMode,
    CancellationToken::Ptr token)
{
    if (frames.empty()) return EncodeResult::Fail("No frames");

    // Declared BEFORE every ComPtr below — see ComApartmentGuard note above.
    ComApartmentGuard com;
    if (!com.Usable()) return EncodeResult::Fail("COM init failed");
    HRESULT hr = S_OK;

    if (token) { try { token->ThrowIfCancelled("GifEncoder::EncodeSequence"); }
                 catch (...) { return EncodeResult::Cancelled(); } }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return EncodeResult::Fail("WIC factory creation failed");

    Microsoft::WRL::ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    // Write NETSCAPE2.0 loop extension
    Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> meta;
    encoder->GetMetadataQueryWriter(&meta);
    if (meta) {
        const BYTE netscape[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
        PROPVARIANT app{}; PropVariantInit(&app);
        app.vt = VT_VECTOR | VT_UI1;
        app.caub.pElems = const_cast<BYTE*>(netscape);
        app.caub.cElems = 11;
        meta->SetMetadataByName(L"/appext/Identification", &app);
        app.caub.pElems = nullptr; app.caub.cElems = 0; app.vt = VT_EMPTY;

        BYTE loopData[4] = { 3, 1,
            static_cast<BYTE>(loopCount & 0xFF),
            static_cast<BYTE>((loopCount >> 8) & 0xFF) };
        PROPVARIANT ld{}; PropVariantInit(&ld);
        ld.vt = VT_VECTOR | VT_UI1;
        ld.caub.pElems = loopData;
        ld.caub.cElems = 4;
        meta->SetMetadataByName(L"/appext/Data", &ld);
        ld.caub.pElems = nullptr; ld.caub.cElems = 0; ld.vt = VT_EMPTY;
    }

    const double frameInterval = 1.0 / std::max(0.1, static_cast<double>(fps));
    const USHORT delayHundredths = static_cast<USHORT>(frameInterval * 100);

    // Disposal = 1 (do not dispose) for every frame. Restore-to-background (2)
    // clears the canvas after each frame, which makes decoders flash the
    // background between frames. Nothing here relies on a background restore —
    // full frames simply overwrite the canvas.
    // (Note: this in-memory path is not used by CaptureSession — AGIF capture
    // goes through EncodeFromFiles — but keep the semantics correct.)
    (void)deltaMode;  // retained in the IEncoder signature
    const BYTE disposalMethod = 1;

    for (size_t i = 0; i < frames.size(); ++i) {
        if (token) { try { token->ThrowIfCancelled("GifEncoder frame loop"); }
                     catch (...) { return EncodeResult::Cancelled(); } }

        const DirectX::Image* img = frames[i].GetImage(0, 0, 0);
        if (!img) { logger::warn("GifEncoder: null frame {}, skipping", i); continue; }

        const WICPixelFormatGUID srcFmt = (img->format == DXGI_FORMAT_R8G8B8A8_UNORM)
            ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;

        Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;
        hr = wic->CreateBitmapFromMemory(
            static_cast<UINT>(img->width), static_cast<UINT>(img->height),
            srcFmt, static_cast<UINT>(img->rowPitch),
            static_cast<UINT>(img->slicePitch), img->pixels, &wicBmp);
        if (FAILED(hr)) { logger::warn("GifEncoder: bitmap create failed for frame {}", i); continue; }

        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEnc;
        Microsoft::WRL::ComPtr<IPropertyBag2> bag;
        encoder->CreateNewFrame(&frameEnc, &bag);
        frameEnc->Initialize(bag.Get());

        // Set frame metadata (delay + disposal)
        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> fmeta;
        if (SUCCEEDED(frameEnc->GetMetadataQueryWriter(&fmeta)) && fmeta) {
            PROPVARIANT pv{}; PropVariantInit(&pv);
            pv.vt = VT_UI2; pv.uiVal = delayHundredths;
            fmeta->SetMetadataByName(L"/grctlext/Delay", &pv);
            PropVariantClear(&pv);
            pv.vt = VT_UI1; pv.bVal = disposalMethod;
            fmeta->SetMetadataByName(L"/grctlext/Disposal", &pv);
            PropVariantClear(&pv);
        }

        frameEnc->SetSize(static_cast<UINT>(img->width), static_cast<UINT>(img->height));
        WICPixelFormatGUID outFmt = GUID_WICPixelFormat32bppBGRA;
        frameEnc->SetPixelFormat(&outFmt);
        frameEnc->WriteSource(wicBmp.Get(), nullptr);
        frameEnc->Commit();
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        logger::error("GifEncoder::EncodeSequence: encoder Commit failed: 0x{:08X}",
                      static_cast<uint32_t>(hr));
        return EncodeResult::Fail("GIF finalize (Commit) failed");
    }
    return EncodeResult::Ok(outputPath);
}

// ============================================================
// EncodeFromFiles — disk-based, memory-efficient path
// Called by CaptureSession after all frames are on disk as BMP.
// ============================================================
EncodeResult GifEncoder::EncodeFromFiles(
    const std::vector<std::wstring>& framePaths,
    const std::wstring& outputPath,
    float fps, int loopCount,
    int deltaMode, int optimize,
    CancellationToken::Ptr token)
{
    logger::info("GifEncoder::EncodeFromFiles: {} frames, fps={}, loopCount={}, deltaMode={}, optimize={}",
                 framePaths.size(), fps, loopCount, deltaMode, optimize);

    if (framePaths.empty()) return EncodeResult::Fail("No frame files");

    // Declared BEFORE every ComPtr below — see ComApartmentGuard note above.
    ComApartmentGuard com;
    if (!com.Usable())
        return EncodeResult::Fail("COM init failed: HRESULT 0x" +
                                  [&]{ char b[16]; sprintf_s(b, "%08X", static_cast<uint32_t>(com.hr)); return std::string(b); }());
    HRESULT hr = S_OK;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return EncodeResult::Fail("WIC factory failed");

    Microsoft::WRL::ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)))
        return EncodeResult::Fail("Cannot open output: " + util::wstring_to_utf8(outputPath));

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    // Write NETSCAPE2.0 extension
    Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> meta;
    encoder->GetMetadataQueryWriter(&meta);
    if (meta) {
        const BYTE netscape[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
        PROPVARIANT app{}; PropVariantInit(&app);
        app.vt = VT_VECTOR | VT_UI1;
        app.caub.pElems = const_cast<BYTE*>(netscape);
        app.caub.cElems = 11;
        meta->SetMetadataByName(L"/appext/Identification", &app);
        app.caub.pElems = nullptr; app.caub.cElems = 0; app.vt = VT_EMPTY;

        BYTE loopData[4] = { 3, 1,
            static_cast<BYTE>(loopCount & 0xFF),
            static_cast<BYTE>((loopCount >> 8) & 0xFF) };
        PROPVARIANT ld{}; PropVariantInit(&ld);
        ld.vt = VT_VECTOR | VT_UI1;
        ld.caub.pElems = loopData;
        ld.caub.cElems = 4;
        meta->SetMetadataByName(L"/appext/Data", &ld);
        ld.caub.pElems = nullptr; ld.caub.cElems = 0; ld.vt = VT_EMPTY;
    }

    const double frameInterval = 1.0 / std::clamp(static_cast<double>(fps), 1.0, 30.0);
    const USHORT delayHundredths = static_cast<USHORT>(std::round(frameInterval * 100.0));

    const bool useDiff  = (deltaMode >= 1);   // 1 or 2 = region/delta encoding
    const bool useDelta = (deltaMode == 2);   // 2 = true delta with transparency

    // Dither selection. Error diffusion re-randomizes its noise pattern when
    // source pixels change even slightly — TAA, lighting and film grain touch
    // nearly every pixel every frame in-game — which reads as full-frame
    // "crawling" grain in an animation and defeats delta detection (the
    // index-level diff sees the dither churn as change, inflating the dirty
    // rectangle to almost the whole frame). Nearest-match keeps indices
    // stable between visually identical frames at the cost of some banding
    // in smooth gradients. Switch to WICBitmapDitherTypeErrorDiffusion if
    // per-frame quality matters more than temporal stability.
    constexpr WICBitmapDitherType kDither = WICBitmapDitherTypeNone;

    // ONE shared palette for the whole animation, built from frame 0.
    //
    // The old code passed prevPalette to the converter together with
    // WICBitmapPaletteTypeMedianCut, which directs the converter to GENERATE
    // a median-cut palette — paletteTranslate must be
    // WICBitmapPaletteTypeCustom for a caller-supplied palette to be used.
    // Every frame was therefore quantized to its own freshly computed
    // palette:
    //   * frame colors shifted slightly frame-to-frame (visible pumping),
    //   * the index-level diff compared indices from DIFFERENT palettes, so
    //     identical pixels produced different indices (bounding box ~ full
    //     frame, delta modes useless),
    //   * "unchanged -> transparent" decisions in true-delta mode were
    //     meaningless across palettes (sparkle).
    //
    // quantPalette holds up to 255 real colors and is used for quantization.
    // framePalette is quantPalette plus one reserved fully-transparent entry
    // appended at index `transparentIdx`, and is what gets written into each
    // GIF frame. Because quantization can only emit indices BELOW
    // transparentIdx, the transparent slot can never collide with a real
    // pixel — the old FindTransparentIndex picked the LEAST-USED index, so
    // real pixels sharing it were punched transparent (holes showing stale
    // canvas content).
    Microsoft::WRL::ComPtr<IWICPalette> quantPalette;
    Microsoft::WRL::ComPtr<IWICPalette> framePalette;
    UINT transparentIdx = 0;

    std::vector<uint8_t> prevIndices;
    UINT                 prevStride = 0;
    size_t               prevWidth = 0, prevHeight = 0;

    for (size_t i = 0; i < framePaths.size(); ++i) {
        if (token) { try { token->ThrowIfCancelled("GifEncoder frame encode"); }
                     catch (...) { return EncodeResult::Cancelled(); } }

        // Load frame from disk
        DirectX::ScratchImage frameImg;
        if (FAILED(DirectX::LoadFromWICFile(framePaths[i].c_str(), DirectX::WIC_FLAGS_NONE, nullptr, frameImg))) {
            logger::warn("GifEncoder: failed to load frame {}", i);
            continue;
        }

        const DirectX::Image* img = frameImg.GetImage(0, 0, 0);
        if (!img) { logger::warn("GifEncoder: null image for frame {}", i); continue; }

        const WICPixelFormatGUID srcFmt = (img->format == DXGI_FORMAT_R8G8B8A8_UNORM)
            ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;

        const UINT fullW = static_cast<UINT>(img->width);
        const UINT fullH = static_cast<UINT>(img->height);

        Microsoft::WRL::ComPtr<IWICBitmap> fullBmp;
        hr = wic->CreateBitmapFromMemory(fullW, fullH, srcFmt,
                static_cast<UINT>(img->rowPitch),
                static_cast<UINT>(img->slicePitch), img->pixels, &fullBmp);
        if (FAILED(hr)) { logger::warn("GifEncoder: bitmap create failed for frame {}", i); continue; }

        // ---- Build the shared palettes once, from the first decodable frame ----
        if (!quantPalette) {
            Microsoft::WRL::ComPtr<IWICPalette> pal;
            wic->CreatePalette(&pal);
            hr = pal->InitializeFromBitmap(fullBmp.Get(), 255, FALSE);
            if (FAILED(hr)) {
                logger::error("GifEncoder: palette generation failed: 0x{:08X}",
                              static_cast<uint32_t>(hr));
                return EncodeResult::Fail("GIF palette generation failed");
            }
            UINT cnt = 0;
            pal->GetColorCount(&cnt);
            std::vector<WICColor> cols(cnt);
            UINT actual = 0;
            pal->GetColors(cnt, cols.data(), &actual);
            cols.resize(actual);

            transparentIdx = actual;               // first index past the real colors
            cols.push_back(0x00000000u);           // reserved fully-transparent entry

            quantPalette = pal;                    // real colors only — used to quantize
            wic->CreatePalette(&framePalette);     // real colors + transparent — written to frames
            framePalette->InitializeCustom(cols.data(), static_cast<UINT>(cols.size()));

            logger::info("GifEncoder: shared palette built ({} colors + transparent index {})",
                         actual, transparentIdx);

            // Set the shared palette as the GIF's GLOBAL color table too, so
            // the global table and every frame's local palette agree on both
            // the colors and the position of the transparent slot. Non-fatal
            // if the encoder declines.
            HRESULT ghr = encoder->SetPalette(framePalette.Get());
            if (FAILED(ghr))
                logger::warn("GifEncoder: encoder->SetPalette (global) not applied: 0x{:08X}",
                             static_cast<uint32_t>(ghr));
        }

        // ---- Quantize against the SHARED palette (WICBitmapPaletteTypeCustom) ----
        Microsoft::WRL::ComPtr<IWICBitmap> qBmp;
        {
            Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
            wic->CreateFormatConverter(&conv);
            hr = conv->Initialize(fullBmp.Get(), GUID_WICPixelFormat8bppIndexed,
                                  kDither, quantPalette.Get(), 0.0,
                                  WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(hr))
                hr = wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, &qBmp);
            if (FAILED(hr) || !qBmp) {
                logger::warn("GifEncoder: quantization failed for frame {} (hr=0x{:08X})",
                             i, static_cast<uint32_t>(hr));
                continue;
            }
        }

        UINT fLeft = 0, fTop = 0, fWidth = fullW, fHeight = fullH;
        Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;   // the bitmap actually written
        bool useTransparency = false;

        WICRect lockRc = { 0, 0, static_cast<INT>(fullW), static_cast<INT>(fullH) };
        Microsoft::WRL::ComPtr<IWICBitmapLock> currLock;
        if (FAILED(qBmp->Lock(&lockRc, WICBitmapLockRead, &currLock))) {
            logger::warn("GifEncoder: lock failed for frame {}", i);
            continue;
        }
        UINT currStride = 0, currBufSz = 0;
        BYTE* currData = nullptr;
        currLock->GetDataPointer(&currBufSz, &currData);
        currLock->GetStride(&currStride);

        // ---- Differential path — indices are now comparable frame-to-frame,
        //      because every frame is quantized against the same palette ----
        if (useDiff && i > 0 && !prevIndices.empty()
            && prevWidth == fullW && prevHeight == fullH)
        {
            UINT minX = fullW, minY = fullH, maxX = 0, maxY = 0;
            bool hasChanges = false;
            for (UINT y = 0; y < fullH; ++y) {
                const uint8_t* cr = currData + y * currStride;
                const uint8_t* pr = prevIndices.data() + y * prevStride;
                for (UINT x = 0; x < fullW; ++x) {
                    if (cr[x] != pr[x]) {
                        hasChanges = true;
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                    }
                }
            }

            if (hasChanges) {
                fLeft = minX; fTop = minY;
                fWidth  = maxX - minX + 1;
                fHeight = maxY - minY + 1;

                std::vector<uint8_t> diffPx(static_cast<size_t>(fWidth) * fHeight);
                for (UINT dy = 0; dy < fHeight; ++dy) {
                    const uint8_t* cr2 = currData + (fTop + dy) * currStride;
                    const uint8_t* pr2 = prevIndices.data() + (fTop + dy) * prevStride;
                    for (UINT dx = 0; dx < fWidth; ++dx) {
                        const UINT sx = fLeft + dx;
                        diffPx[static_cast<size_t>(dy) * fWidth + dx] =
                            (useDelta && cr2[sx] == pr2[sx])
                                ? static_cast<uint8_t>(transparentIdx)  // reserved — collision impossible
                                : cr2[sx];
                    }
                }
                useTransparency = useDelta;

                hr = wic->CreateBitmap(fWidth, fHeight, GUID_WICPixelFormat8bppIndexed,
                                       WICBitmapCacheOnDemand, &wicBmp);
                if (SUCCEEDED(hr)) {
                    wicBmp->SetPalette(framePalette.Get());
                    WICRect wr = { 0, 0, static_cast<INT>(fWidth), static_cast<INT>(fHeight) };
                    Microsoft::WRL::ComPtr<IWICBitmapLock> wl;
                    if (SUCCEEDED(wicBmp->Lock(&wr, WICBitmapLockWrite, &wl))) {
                        UINT ws = 0, wbsz = 0; BYTE* wd = nullptr;
                        wl->GetDataPointer(&wbsz, &wd); wl->GetStride(&ws);
                        for (UINT y = 0; y < fHeight; ++y)
                            std::memcpy(wd + y * ws,
                                        diffPx.data() + static_cast<size_t>(y) * fWidth, fWidth);
                    } else {
                        wicBmp.Reset();   // fall through to full-frame emit
                    }
                }
                if (!wicBmp) {
                    fLeft = 0; fTop = 0; fWidth = fullW; fHeight = fullH;
                    useTransparency = false;
                }
            }
            // No changes at all -> fall through and re-emit the full quantized
            // frame. Identical indices + identical palette render identically,
            // so this costs a little size but cannot flicker.
        }

        // Remember this frame's indices for the next diff.
        prevStride = currStride;
        prevIndices.assign(currData, currData + static_cast<size_t>(fullH) * currStride);
        prevWidth = fullW; prevHeight = fullH;
        currLock.Reset();

        // ---- Full-frame path (frame 0, deltaMode 0, no-change, or fallback) ----
        if (!wicBmp) {
            // The old deltaMode==0 path handed the raw 32bpp BGRA bitmap to
            // WriteSource with SetPixelFormat(8bppIndexed) and NO palette, so
            // the WIC GIF encoder did its own per-frame conversion with an
            // uncontrolled default palette and no adaptive quantization —
            // that alone produced the heavy grain in "Delta Mode: Off".
            // Every path now writes properly quantized, shared-palette frames.
            wicBmp = qBmp;
            wicBmp->SetPalette(framePalette.Get());
        }

        // ---- Write this frame into the GIF ----
        // Every HRESULT here is now checked. Previously they were all
        // ignored: if anything about a frame failed at commit time, the loop
        // continued, the encoder entered an error state, the NEXT
        // CreateNewFrame returned a NULL frameEnc, and the unconditional
        // frameEnc->Initialize() dereferenced it — killing the worker with no
        // callback, no log line, and no finalized file ("no result").
        // Any failure now aborts with a message naming the call and HRESULT.
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEnc;
        Microsoft::WRL::ComPtr<IPropertyBag2> bag;
        hr = encoder->CreateNewFrame(&frameEnc, &bag);
        if (FAILED(hr) || !frameEnc) {
            logger::error("GifEncoder: CreateNewFrame failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame creation failed");
        }
        hr = frameEnc->Initialize(bag.Get());
        if (FAILED(hr)) {
            logger::error("GifEncoder: frame Initialize failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame init failed");
        }

        // Metadata: delay + disposal + (optional) transparency
        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> fmeta;
        if (SUCCEEDED(frameEnc->GetMetadataQueryWriter(&fmeta)) && fmeta) {
            PROPVARIANT pv{}; PropVariantInit(&pv);
            pv.vt = VT_UI2; pv.uiVal = delayHundredths;
            fmeta->SetMetadataByName(L"/grctlext/Delay", &pv); PropVariantClear(&pv);

            // Disposal = 1 (do not dispose) for EVERY frame. The old code used
            // 2 (restore to background) for full frames — including frame 0
            // and any "no change" frame — which tells the decoder to CLEAR the
            // whole canvas after showing it. The next delta region was then
            // composited onto a blank canvas: everything outside the dirty
            // rectangle flashed to background once per frame. That was the
            // severe flicker. Nothing in this encoder relies on a background
            // restore — full frames simply overwrite the canvas.
            pv.vt = VT_UI1; pv.bVal = 1;
            fmeta->SetMetadataByName(L"/grctlext/Disposal", &pv); PropVariantClear(&pv);

            if (useTransparency) {
                // /grctlext/TransparencyFlag is VT_BOOL in the WIC GIF
                // metadata schema — NOT VT_UI1. The old code wrote it as
                // VT_UI1, which the GIF metadata writer rejects with
                // WINCODEC_ERR_UNEXPECTEDMETADATATYPE, and the HRESULT was
                // never checked — so the flag silently never reached the
                // Graphic Control Extension. Every "unchanged -> transparent"
                // pixel then rendered as OPAQUE palette color 0x00000000
                // (black), and with disposal = do-not-dispose the black
                // accumulated on the canvas frame after frame: the severe
                // distortion/strobing seen in DeltaMode 2.
                pv.vt = VT_BOOL; pv.boolVal = VARIANT_TRUE;
                HRESULT mhr = fmeta->SetMetadataByName(L"/grctlext/TransparencyFlag", &pv);
                PropVariantClear(&pv);
                if (FAILED(mhr))
                    logger::error("GifEncoder: TransparencyFlag write failed for frame {}: 0x{:08X}",
                                  i, static_cast<uint32_t>(mhr));

                pv.vt = VT_UI1; pv.bVal = static_cast<BYTE>(transparentIdx);
                mhr = fmeta->SetMetadataByName(L"/grctlext/TransparentColorIndex", &pv);
                PropVariantClear(&pv);
                if (FAILED(mhr))
                    logger::error("GifEncoder: TransparentColorIndex write failed for frame {}: 0x{:08X}",
                                  i, static_cast<uint32_t>(mhr));
            }

            // Frame offset within the canvas
            pv.vt = VT_UI2; pv.uiVal = static_cast<USHORT>(fLeft);
            fmeta->SetMetadataByName(L"/imgdesc/Left", &pv); PropVariantClear(&pv);
            pv.vt = VT_UI2; pv.uiVal = static_cast<USHORT>(fTop);
            fmeta->SetMetadataByName(L"/imgdesc/Top", &pv); PropVariantClear(&pv);
        }

        hr = frameEnc->SetSize(fWidth, fHeight);
        if (FAILED(hr)) {
            logger::error("GifEncoder: SetSize({}x{}) failed for frame {}: 0x{:08X}",
                          fWidth, fHeight, i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame SetSize failed");
        }

        WICPixelFormatGUID outFmt = GUID_WICPixelFormat8bppIndexed;
        hr = frameEnc->SetPixelFormat(&outFmt);
        if (FAILED(hr)) {
            logger::error("GifEncoder: SetPixelFormat failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame SetPixelFormat failed");
        }

        // Explicitly set the frame's local palette. WriteSource CAN adopt the
        // source bitmap's palette, but the documented-reliable path for
        // indexed GIF frames (and what Microsoft's animated-GIF sample does)
        // is an explicit IWICBitmapFrameEncode::SetPalette. With the GCE
        // transparency flag now actually being serialized in DeltaMode 2, an
        // encoder-chosen fallback palette would silently mismatch the pixel
        // indices and the TransparentColorIndex.
        hr = frameEnc->SetPalette(framePalette.Get());
        if (FAILED(hr)) {
            logger::error("GifEncoder: frame SetPalette failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame SetPalette failed");
        }

        // wicBmp is always exactly fWidth x fHeight now (the region bitmap or
        // the full quantized frame), so no source rect is needed.
        hr = frameEnc->WriteSource(wicBmp.Get(), nullptr);
        if (FAILED(hr)) {
            logger::error("GifEncoder: WriteSource failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame WriteSource failed");
        }

        hr = frameEnc->Commit();
        if (FAILED(hr)) {
            logger::error("GifEncoder: frame Commit failed for frame {}: 0x{:08X}",
                          i, static_cast<uint32_t>(hr));
            return EncodeResult::Fail("GIF frame Commit failed");
        }

        logger::debug("GifEncoder: encoded frame {}/{}", i + 1, framePaths.size());
    }

    // A failed Commit means the GIF trailer/stream was never finalized — the
    // file on disk is broken. Previously this returned Ok unconditionally and
    // the session reported CALLBACK_SUCCESS for an unreadable file.
    hr = encoder->Commit();
    if (FAILED(hr)) {
        logger::error("GifEncoder::EncodeFromFiles: encoder Commit failed: 0x{:08X}",
                      static_cast<uint32_t>(hr));
        return EncodeResult::Fail("GIF finalize (Commit) failed");
    }
    logger::info("GifEncoder::EncodeFromFiles: complete -> {}", util::wstring_to_utf8(outputPath));
    return EncodeResult::Ok(outputPath);
}
