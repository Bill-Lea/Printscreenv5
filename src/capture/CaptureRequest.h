#pragma once
#include <string>

// ============================================================
// Format enumerations (replaces ScreenCapture::ImageFormat etc.)
// ============================================================
enum class ImageFormat  { PNG = 0, JPEG, BMP, TIF, GIF, AGIF, APNG, DDS, H264 };
enum class TiffMode     { NONE = 0, LZW, CCITT1D, CCITT4, RLE, ZIP };
enum class DDSMode      {
    BC1 = 0, BC2, BC3, BC4, BC5, BC6H,
    BC7_SLOW, BC7_NORMAL, BC7_FAST
};

// ============================================================
// Format string helpers
// ============================================================
ImageFormat ParseImageFormat(const std::string& s);
TiffMode    ParseTiffMode(const std::string& s);
DDSMode     ParseDDSMode(const std::string& s);
std::wstring FormatToExtension(ImageFormat fmt);

// ============================================================
// CaptureRequest — all parameters for one capture operation
// ============================================================
struct CaptureRequest {
    std::wstring outputDir;

    ImageFormat format      = ImageFormat::PNG;
    float       jpegQuality = 95.0f;           // 0–100
    TiffMode    tiffMode    = TiffMode::NONE;
    DDSMode     ddsMode     = DDSMode::BC1;

    // Animated formats (GIF / APNG)
    float animDuration   = 3.0f;   // seconds
    float animFPS        = 10.0f;  // frames per second
    int   loopCount      = 0;      // 0 = infinite
    int   deltaMode      = 0;      // 0 = off (full frames), 1 = region extraction, 2 = true delta (with transparency)
    int   optimize       = 0;      // 0 = off, 1 = on (transparency optimization for delta frames)
    int   pngCompression = 6;      // PNG zlib compression level 0-9 (for still PNG captures)

    // Video capture (H264)
    // targetResolution: 0=Native, 1=720p, 2=1080p, 3=1440p, 4=4K
    float  videoDuration        = 10.0f;   // seconds (1–120)
    int    targetResolution     = 0;        // output resolution (0–4)
    int    videoFrameRate       = 30;       // 30 or 60
    int    qualityPreset        = 2;        // 0=Low, 1=Medium, 2=High, 3=VeryHigh, 4=Custom
    int    videoBitrateKbps     = 8000;    // kbps (Custom preset only)
    float  keyframeIntervalSec  = 2.0f;    // seconds
    int    encoderPreference    = 0;       // 0=Auto, 1=PreferHW, 2=ForceSW
    int    rateControl          = 1;       // 0=CBR, 1=VBR, 2=CQP
    int    videoContainer       = 0;       // 0=MP4

    // UI visibility control
    // true  = hide HUD/menus before capture, restore after (Papyrus "Menu" property;
    //         done via the UIController Scaleform flag, not a console command)
    // false = leave UI as-is
    bool autoUI = true;
};
