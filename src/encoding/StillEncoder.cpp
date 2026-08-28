#include "PCH.h"
#include "StillEncoder.h"
#include "stringutils.h"
#include <DirectXTex.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <nlohmann/json.hpp>
#include <filesystem>

StillEncoder::StillEncoder(const CaptureRequest& req)
    : format_(req.format), jpegQuality_(req.jpegQuality),
      tiffMode_(req.tiffMode), ddsMode_(req.ddsMode),
      pngCompression_(req.pngCompression) {}

// ============================================================
// Public interface
// ============================================================

EncodeResult StillEncoder::Encode(
    const DirectX::ScratchImage& frame,
    const std::wstring& outputPath,
    CancellationToken::Ptr token,
    const CaptureExif&   exif)
{
    if (token) token->ThrowIfCancelled("StillEncoder::Encode start");

    HRESULT hr = (format_ == ImageFormat::DDS)
        ? SaveDDS(frame, outputPath, token)
        : SaveWIC(frame, outputPath, token, exif);

    if (hr == E_ABORT)     return EncodeResult::Cancelled();
    if (FAILED(hr))        return EncodeResult::Fail(
        "Save failed: HRESULT 0x" + [&]{ char buf[16]; sprintf_s(buf, "%08X", hr); return std::string(buf); }());

    return EncodeResult::Ok(outputPath);
}

EncodeResult StillEncoder::EncodeSequence(
    std::vector<DirectX::ScratchImage>& frames,
    const std::wstring& outputPath,
    float, int, int,
    CancellationToken::Ptr token)
{
    if (frames.empty()) return EncodeResult::Fail("No frames provided");
    return Encode(frames[0], outputPath, token);
}

// ============================================================
// WIC save (PNG / JPEG / BMP / TIF / GIF single-frame)
// ============================================================
// ============================================================
// EXIF metadata writing via WIC fast-metadata-encoder
// ============================================================
//
// After the image is saved we reopen it with a WIC decoder, create a
// fast metadata encoder, and write standard EXIF IFD tags:
//
//   Tag 271   Make                     (ASCII)
//   Tag 272   Model                    (ASCII)
//   Tag 305   Software                 (ASCII)
//   Tag 37386 FocalLength              (RATIONAL mm)
//   Tag 41989 FocalLengthIn35mmFormat  (SHORT mm)
//
// JPEG and TIFF receive full EXIF IFD support via WIC.
// PNG gets limited support — WIC writes tEXt chunks, not true EXIF
// IFD — so we also emit a sidecar .json file for PNG as a fallback
// for PhotoFileMerge V2's ImageLoader sidecar path.
// ============================================================

namespace {

HRESULT WriteExifMetadata(const std::wstring& path, const CaptureExif& exif)
{
    // Only write if we have something useful.
    if (exif.focalLength35mm <= 0.0 && exif.cameraMake.empty() && exif.cameraModel.empty())
        return S_OK;

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    // Create decoder from the file we just wrote.
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ | GENERIC_WRITE,
        WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) return hr;

    // Create a fast metadata encoder from the decoder.
    // This patches metadata in-place without re-encoding the image.
    Microsoft::WRL::ComPtr<IWICFastMetadataEncoder> fastEnc;
    hr = factory->CreateFastMetadataEncoderFromDecoder(
        decoder.Get(), fastEnc.GetAddressOf());
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> writer;
    hr = fastEnc->GetMetadataQueryWriter(writer.GetAddressOf());
    if (FAILED(hr)) return hr;

    PROPVARIANT pv;
    PropVariantInit(&pv);

    auto setString = [&](const wchar_t* query, const std::string& value) {
        if (value.empty()) return;
        // Convert UTF-8 to wide for WIC.
        int wlen = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return;
        std::wstring wv(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wv.data(), wlen);
        PropVariantClear(&pv);
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<LPWSTR>(wv.c_str());
        std::ignore = writer->SetMetadataByName(query, &pv);
    };

    // Make (tag 271)
    setString(L"/app1/ifd/{ushort=271}", exif.cameraMake);

    // Model (tag 272)
    setString(L"/app1/ifd/{ushort=272}", exif.cameraModel);

    // Software (tag 305)
    setString(L"/app1/ifd/{ushort=305}", exif.software);

    // FocalLength (tag 37386) — EXIF RATIONAL, in mm.
    // WIC accepts VT_R8 for rational fields and converts internally.
    if (exif.focalLength35mm > 0.0) {
        PropVariantClear(&pv);
        pv.vt = VT_R8;
        pv.dblVal = exif.focalLength35mm;
        std::ignore = writer->SetMetadataByName(L"/app1/ifd/exif/{ushort=37386}", &pv);

        // FocalLengthIn35mmFormat (tag 41989) — SHORT, in mm.
        PropVariantClear(&pv);
        pv.vt = VT_UI2;
        pv.uiVal = static_cast<USHORT>(std::round(exif.focalLength35mm));
        std::ignore = writer->SetMetadataByName(L"/app1/ifd/exif/{ushort=41989}", &pv);
    }

    PropVariantClear(&pv);

    hr = fastEnc->Commit();
    if (FAILED(hr)) {
        logger::warn("WriteExifMetadata: fast-encoder commit failed (0x{:08X}), "
                     "image saved without EXIF",
                     static_cast<uint32_t>(hr));
    }
    return hr;
}

// Write a sidecar .json file next to the image for PFM V2's sidecar path.
// PFM V2 reads focalLength35mm from <image>.json when EXIF is absent.
void WriteSidecarJson(const std::wstring& imagePath, const CaptureExif& exif)
{
    if (exif.focalLength35mm <= 0.0) return;

    std::filesystem::path p(imagePath);
    p += ".json";

    nlohmann::json j;
    j["focalLength35mm"] = exif.focalLength35mm;
    if (exif.fovDegrees > 0.0)
        j["fov"] = exif.fovDegrees;
    if (!exif.cameraMake.empty())
        j["cameraMake"] = exif.cameraMake;
    if (!exif.cameraModel.empty())
        j["cameraModel"] = exif.cameraModel;
    if (exif.imageWidth > 0)
        j["imageWidth"] = exif.imageWidth;
    if (exif.imageHeight > 0)
        j["imageHeight"] = exif.imageHeight;

    std::ofstream sf(p);
    if (sf.is_open()) {
        sf << j.dump(2);
        logger::info("WriteSidecarJson: wrote {}", p.string());
    } else {
        logger::warn("WriteSidecarJson: could not open {}", p.string());
    }
}

} // anonymous namespace

HRESULT StillEncoder::SaveWIC(
    const DirectX::ScratchImage& img,
    const std::wstring& path,
    CancellationToken::Ptr token,
    const CaptureExif&    exif)
{
    logger::info("StillEncoder: WIC save, format={}", static_cast<int>(format_));
    if (token) token->ThrowIfCancelled("StillEncoder::SaveWIC start");

    GUID containerFmt{};
    switch (format_) {
        case ImageFormat::PNG:  containerFmt = GUID_ContainerFormatPng;  break;
        case ImageFormat::JPEG: containerFmt = GUID_ContainerFormatJpeg; break;
        case ImageFormat::BMP:  containerFmt = GUID_ContainerFormatBmp;  break;
        case ImageFormat::TIF:  containerFmt = GUID_ContainerFormatTiff; break;
        case ImageFormat::GIF:  containerFmt = GUID_ContainerFormatGif;  break;
        default:
            logger::error("StillEncoder::SaveWIC: unhandled format {}", static_cast<int>(format_));
            return E_INVALIDARG;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInited = SUCCEEDED(hr);

    auto guard = [&](HRESULT result) {
        if (comInited) CoUninitialize();
        return result;
    };

    // From this point on, cancellation must go through guard() — throwing
    // here would skip the CoUninitialize above (guard is a lambda, not RAII)
    // and leak a COM init on every cancelled still capture. E_ABORT is mapped
    // back to EncodeResult::Cancelled() by Encode().
    if (token && token->IsCancelled()) return guard(E_ABORT);

    const DirectX::Image* srcImg = img.GetImage(0, 0, 0);
    if (!srcImg) return guard(E_FAIL);

    if (format_ == ImageFormat::JPEG) {
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str(), nullptr,
            [&](IPropertyBag2* props) -> HRESULT {
                if (!props) return S_OK;
                PROPBAG2 opt{};
                opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                VARIANT v{}; VariantInit(&v);
                v.vt = VT_R4;
                v.fltVal = std::clamp(jpegQuality_, 0.f, 100.f) / 100.0f;
                return props->Write(1, &opt, &v);
            });
    } else if (format_ == ImageFormat::TIF) {
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str(), nullptr,
            [&](IPropertyBag2* props) -> HRESULT {
                if (!props) return S_OK;
                PROPBAG2 opt{};
                opt.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
                VARIANT v{}; VariantInit(&v);
                v.vt = VT_UI1;
                switch (tiffMode_) {
                    case TiffMode::LZW:     v.bVal = static_cast<BYTE>(WICTiffCompressionLZW);    break;
                    case TiffMode::ZIP:     v.bVal = static_cast<BYTE>(WICTiffCompressionZIP);    break;
                    case TiffMode::RLE:     v.bVal = static_cast<BYTE>(WICTiffCompressionRLE);    break;
                    case TiffMode::CCITT1D: v.bVal = static_cast<BYTE>(WICTiffCompressionCCITT3); break;
                    case TiffMode::CCITT4:  v.bVal = static_cast<BYTE>(WICTiffCompressionCCITT4); break;
                    default:                v.bVal = static_cast<BYTE>(WICTiffCompressionNone);   break;
                }
                return props->Write(1, &opt, &v);
            });
    } else if (format_ == ImageFormat::PNG) {
        // WIC PNG encoder doesn't expose a direct zlib level, but we can
        // influence compression via the PngFilterMethod property bag.
        // Higher compression levels benefit from adaptive filtering.
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str(), nullptr,
            [&](IPropertyBag2* props) -> HRESULT {
                if (!props) return S_OK;
                // PngFilterMethod: 0=default, 1=none, 2=sub, 3=up, 4=average, 5=paeth, 6=adaptive
                // For compression 0-2: use no filter (fastest)
                // For compression 3-6: use adaptive (good balance)
                // For compression 7-9: use adaptive (best compression)
                PROPBAG2 opt{};
                opt.pstrName = const_cast<LPOLESTR>(L"PngFilterMethod");
                VARIANT v{}; VariantInit(&v);
                v.vt = VT_UI1;
                v.bVal = (pngCompression_ <= 2) ? 1 : 6;  // none or adaptive
                return props->Write(1, &opt, &v);
            });
    } else {
        // BMP / GIF / other: save with default WIC options
        hr = DirectX::SaveToWICFile(
            *srcImg, DirectX::WIC_FLAGS_NONE,
            containerFmt, path.c_str());
    }

    if (FAILED(hr)) logger::error("StillEncoder::SaveWIC failed: 0x{:08X}", static_cast<uint32_t>(hr));
    else {
        logger::info("StillEncoder::SaveWIC saved: {}", util::wstring_to_utf8(path));

        // Write EXIF metadata into the saved file (JPEG / TIFF / PNG).
        // BMP has no metadata container — skip silently.
        if (exif.focalLength35mm > 0.0 && format_ != ImageFormat::BMP) {
            HRESULT exifHr = WriteExifMetadata(path, exif);
            if (SUCCEEDED(exifHr)) {
                logger::info("StillEncoder: EXIF written (f={:.1f}mm, fov={:.1f}°)",
                             exif.focalLength35mm, exif.fovDegrees);
            }
            // Always write sidecar JSON for PNG (WIC PNG EXIF is unreliable
            // for LibRaw consumption) and as a universal fallback.
            if (format_ == ImageFormat::PNG) {
                WriteSidecarJson(path, exif);
            }
        }
    }

    return guard(hr);
}

// ============================================================
// DDS save (BC1–BC7 via DirectXTex)
// ============================================================
HRESULT StillEncoder::SaveDDS(
    const DirectX::ScratchImage& img,
    const std::wstring& path,
    CancellationToken::Ptr token)
{
    logger::info("StillEncoder: DDS save, mode={}", static_cast<int>(ddsMode_));
    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS start");

    DXGI_FORMAT compFmt{};
    DirectX::TEX_COMPRESS_FLAGS compFlags = DirectX::TEX_COMPRESS_DEFAULT;

    switch (ddsMode_) {
        case DDSMode::BC1:       compFmt = DXGI_FORMAT_BC1_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC2:       compFmt = DXGI_FORMAT_BC2_UNORM; break;
        case DDSMode::BC3:       compFmt = DXGI_FORMAT_BC3_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC4:       compFmt = DXGI_FORMAT_BC4_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC5:       compFmt = DXGI_FORMAT_BC5_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC6H:      compFmt = DXGI_FORMAT_BC6H_UF16; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC7_SLOW:  compFmt = DXGI_FORMAT_BC7_UNORM; compFlags = DirectX::TEX_COMPRESS_PARALLEL; break;
        case DDSMode::BC7_NORMAL:
        case DDSMode::BC7_FAST:
            compFmt   = DXGI_FORMAT_BC7_UNORM;
            compFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                DirectX::TEX_COMPRESS_PARALLEL | DirectX::TEX_COMPRESS_BC7_QUICK);
            break;
        default:
            compFmt   = DXGI_FORMAT_BC1_UNORM;
            compFlags = DirectX::TEX_COMPRESS_PARALLEL;
            break;
    }

    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS before compression");

    const DirectX::Image* srcImg = img.GetImage(0, 0, 0);
    if (!srcImg) return E_FAIL;

    const auto t0 = std::chrono::high_resolution_clock::now();

    DirectX::ScratchImage compressed;
    HRESULT hr = DirectX::Compress(*srcImg, compFmt, compFlags,
                                   DirectX::TEX_THRESHOLD_DEFAULT, compressed);

    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - t0);

    if (token) token->ThrowIfCancelled("StillEncoder::SaveDDS after compression");

    const DirectX::ScratchImage* toSave = SUCCEEDED(hr) ? &compressed : &img;

    if (FAILED(hr)) {
        logger::warn("StillEncoder::SaveDDS: BC compression failed ({} ms), saving uncompressed", dur.count());
    } else {
        float ratio = static_cast<float>(srcImg->slicePitch) /
                      static_cast<float>(compressed.GetImage(0,0,0)->slicePitch);
        logger::info("StillEncoder::SaveDDS: compressed in {} ms, ratio {:.2f}:1", dur.count(), ratio);
    }

    hr = DirectX::SaveToDDSFile(toSave->GetImages(), toSave->GetImageCount(),
                                 toSave->GetMetadata(), DirectX::DDS_FLAGS_NONE,
                                 path.c_str());
    if (FAILED(hr))
        logger::error("StillEncoder::SaveDDS: SaveToDDSFile failed: 0x{:08X}", static_cast<uint32_t>(hr));
    else
        logger::info("StillEncoder::SaveDDS saved: {}", util::wstring_to_utf8(path));

    return hr;
}
