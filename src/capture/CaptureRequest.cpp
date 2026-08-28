#include "PCH.h"
#include "CaptureRequest.h"
#include <algorithm>
#include <cctype>

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

ImageFormat ParseImageFormat(const std::string& s) {
    const auto lo = ToLower(s);
    if (lo == "png")                                   return ImageFormat::PNG;
    if (lo == "jpg" || lo == "jpeg")                   return ImageFormat::JPEG;
    if (lo == "bmp")                                   return ImageFormat::BMP;
    if (lo == "tif" || lo == "tiff")                   return ImageFormat::TIF;
    if (lo == "gif")                                    return ImageFormat::GIF;
    if (lo == "agif" || lo == "animated_gif"
        || lo == "gif_animated" || lo == "animatedgif") return ImageFormat::AGIF;
    if (lo == "apng" || lo == "animated_png"
        || lo == "png_animated" || lo == "animatedpng") return ImageFormat::APNG;
    if (lo == "dds")                                   return ImageFormat::DDS;
    if (lo == "h264" || lo == "mp4" || lo == "video")   return ImageFormat::H264;
    logger::error("ParseImageFormat: unknown format '{}', defaulting to PNG", s);
    return ImageFormat::PNG;
}

TiffMode ParseTiffMode(const std::string& s) {
    const auto lo = ToLower(s);
    if (lo == "lzw")     return TiffMode::LZW;
    if (lo == "ccitt1d") return TiffMode::CCITT1D;
    if (lo == "ccitt4")  return TiffMode::CCITT4;
    if (lo == "rle")     return TiffMode::RLE;
    if (lo == "zip")     return TiffMode::ZIP;
    return TiffMode::NONE;
}

DDSMode ParseDDSMode(const std::string& s) {
    const auto lo = ToLower(s);
    if (lo == "bc1")       return DDSMode::BC1;
    if (lo == "bc2")       return DDSMode::BC2;
    if (lo == "bc3")       return DDSMode::BC3;
    if (lo == "bc4")       return DDSMode::BC4;
    if (lo == "bc5")       return DDSMode::BC5;
    if (lo == "bc6h")      return DDSMode::BC6H;
    if (lo == "bc7_slow")  return DDSMode::BC7_SLOW;
    if (lo == "bc7_normal" || lo == "bc7") return DDSMode::BC7_NORMAL;
    if (lo == "bc7_fast")  return DDSMode::BC7_FAST;
    return DDSMode::BC1;
}

std::wstring FormatToExtension(ImageFormat fmt) {
    switch (fmt) {
        case ImageFormat::PNG:  return L".png";
        case ImageFormat::JPEG: return L".jpg";
        case ImageFormat::BMP:  return L".bmp";
        case ImageFormat::TIF:  return L".tif";
        case ImageFormat::GIF:  return L".gif";
        case ImageFormat::AGIF: return L".gif";
        case ImageFormat::APNG: return L".png";
        case ImageFormat::DDS:  return L".dds";
        case ImageFormat::H264: return L".mp4";
        default:
            logger::error("FormatToExtension: unhandled format {}", static_cast<int>(fmt));
            return L".png";
    }
}
