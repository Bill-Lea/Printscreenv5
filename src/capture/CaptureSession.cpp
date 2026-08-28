#include "PCH.h"
#include "CaptureSession.h"
#include "FrameAcquirer.h"
#include "GPUScaler.h"
#include "FrameQueue.h"
#include "encoding/StillEncoder.h"
#include "encoding/GifEncoder.h"
#include "encoding/ApngEncoder.h"
#include "../VideoCapture.h"
#include "stringutils.h"
#include "TempFileGuard.h"
#include "GameCamera.h"
#include <filesystem>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <windows.h>
#include <thread>

// Frame timing constant (100-nanosecond units per second)
constexpr LONGLONG kHnsPerSecond = 10'000'000LL;

// ============================================================
// Internal helpers
// ============================================================
namespace {

// Forward declarations
static float ClampDurationForType(ImageFormat fmt, float duration);
static Printscreen::VideoCaptureConfig BuildVideoConfig(const CaptureRequest& req, uint32_t dstWidth, uint32_t dstHeight);
static bool ResolveTargetResolution(int targetResolution, uint32_t desktopW, uint32_t desktopH,
                                    uint32_t& outW, uint32_t& outH);

static std::wstring MakeOutputPath(const std::wstring& outputDir, ImageFormat fmt) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);

    std::wostringstream ss;
    ss << L"SS_"
       << std::put_time(&tm_buf, L"%Y%m%d_%H%M%S")
       << L"_" << std::setfill(L'0') << std::setw(3) << ms.count()
       << FormatToExtension(fmt);

    return (std::filesystem::path(outputDir) / ss.str()).wstring();
}

static std::wstring MakeTempDir(const std::wstring& outputDir, const wchar_t* prefix) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);

    wchar_t ts[64];
    swprintf_s(ts, L"%s_%04d%02d%02d_%02d%02d%02d_%03d",
               prefix,
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
               static_cast<int>(ms.count()));

    std::wstring dir = (std::filesystem::path(outputDir) / ts).wstring();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) { logger::error("MakeTempDir: {}", ec.message()); return {}; }
    return dir;
}

// Capture frames to disk and return list of BMP paths.
// Throws Cancelled or std::runtime_error on failure.
static std::vector<std::wstring> CaptureFramesToDisk(
    const CaptureRequest& req,
    const std::wstring& tempDir,
    FrameAcquirer& acquirer,
    CancellationToken::Ptr token)
{
    const double fps         = std::clamp(static_cast<double>(req.animFPS), 1.0, 60.0);
    const float  duration    = ClampDurationForType(req.format, req.animDuration);
    const int    totalFrames = std::max(1, static_cast<int>(std::round(duration * fps)));
    const double interval    = 1.0 / fps;

    logger::info("CaptureFramesToDisk: {} frames at {} fps", totalFrames, fps);

    std::vector<std::wstring> paths;
    paths.reserve(static_cast<size_t>(totalFrames));
    const auto startTime = std::chrono::high_resolution_clock::now();

    for (int f = 0; f < totalFrames; ++f) {
        if (token) token->ThrowIfCancelled("frame capture loop");

        DirectX::ScratchImage img;
        HRESULT hr = acquirer.AcquireFrame(img);
        if (hr == E_ABORT) throw Cancelled{};
        if (FAILED(hr))    throw std::runtime_error("AcquireFrame failed");

        wchar_t name[32]; swprintf_s(name, L"frame_%04d.bmp", f);
        std::wstring path = (std::filesystem::path(tempDir) / name).wstring();

        const DirectX::Image* di = img.GetImage(0, 0, 0);
        if (!di) throw std::runtime_error("Null image from acquirer");
        HRESULT savehr = DirectX::SaveToWICFile(*di, DirectX::WIC_FLAGS_NONE,
                                                GUID_ContainerFormatBmp, path.c_str());
        if (FAILED(savehr)) throw std::runtime_error("Failed to save temp BMP");

        paths.push_back(std::move(path));
        img.Release();

        // Frame-rate-governed pacing. A plain sleep in short slices keeps
        // cancellation latency bounded (~15ms) without the previous
        // static mutex/condition_variable pair, which was shared across
        // all captures and was never notified by anyone — cancellation
        // could only be observed when the wait timed out.
        const auto targetTime = startTime + std::chrono::duration<double>((f + 1) * interval);
        while (std::chrono::high_resolution_clock::now() < targetTime) {
            if (token) token->ThrowIfCancelled("frame timing wait");
            const auto remaining = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
                targetTime - std::chrono::high_resolution_clock::now());
            const auto sleepSlice = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::milliseconds(15));
            std::this_thread::sleep_for(std::min(remaining, sleepSlice));
        }
    }
    return paths;
}

// ---- Run one animated capture ----
static std::string RunAnimated(const CaptureRequest& req, CancellationToken::Ptr token,
                                const CaptureSession::AcquisitionCallback& onAcquired) {
    const wchar_t* prefix = (req.format == ImageFormat::AGIF) ? L"gif_temp" : L"apng_temp";
    
    // Clean up any stale temp directories from previous crashed sessions
    // in the same output directory
    StaleTempCleanup::ScanAndRemove(req.outputDir);
    
    std::wstring tempDir  = MakeTempDir(req.outputDir, prefix);
    if (tempDir.empty()) return "CALLBACK_ERROR: Failed to create temp directory";

    // RAII guard ensures cleanup even if we crash during encoding
    TempFileGuard tempGuard(tempDir);
    std::vector<std::wstring> tempFiles;
    
    try {
        FrameAcquirer acquirer(token);
        HRESULT hr = acquirer.Initialize();
        if (hr == E_ABORT) return "CALLBACK_CANCELLED";
        if (FAILED(hr))    return "CALLBACK_ERROR: Desktop duplication init failed";

        tempFiles = CaptureFramesToDisk(req, tempDir, acquirer, token);
    } catch (const Cancelled&) {
        return "CALLBACK_CANCELLED";
    } catch (const std::exception& e) {
        return std::string("CALLBACK_ERROR: ") + e.what();
    }

    if (tempFiles.empty()) {
        return "CALLBACK_ERROR: No frames captured";
    }

    if (token && token->IsCancelled()) {
        return "CALLBACK_CANCELLED";
    }

    // All frames acquired — restore UI now so encoding (which can be slow for
    // many-frame AGIF/APNG) does not keep the HUD suppressed unnecessarily.
    if (onAcquired) onAcquired();

    std::wstring outPath = MakeOutputPath(req.outputDir, req.format);
    EncodeResult er;

    if (req.format == ImageFormat::AGIF) {
        GifEncoder enc;
        er = enc.EncodeFromFiles(tempFiles, outPath, req.animFPS, req.loopCount,
                                  req.deltaMode, req.optimize, token);
    } else {
        ApngEncoder enc;
        er = enc.EncodeFromFiles(tempFiles, outPath, req.animFPS, req.loopCount,
                                  req.deltaMode, req.optimize, token);
    }

    // Encoding complete — clean up temp frames via the guard's Cleanup().
    // The old code called a local CleanupTempFiles() and then unconditionally
    // Release()d the guard, which disarmed the destructor pass its own comment
    // promised: if any BMP was still locked (AV scan, search indexer) the file
    // AND the directory were orphaned permanently. It also removed only the
    // tracked file list with a non-recursive directory remove, so any
    // untracked file in the temp dir silently kept the dir alive.
    // Cleanup() removes the whole directory tree and only disarms the guard
    // once the directory is verified gone — otherwise the destructor retries,
    // and the dir stays in the reload-cleanup registry until actually removed.
    tempGuard.Cleanup();

    if (!er.success) {
        return (er.error == "Cancelled") ? "CALLBACK_CANCELLED"
                                         : "CALLBACK_ERROR: " + er.error;
    }
    return "CALLBACK_SUCCESS";
}

// ---- Run one still capture ----
static std::string RunStill(const CaptureRequest& req, CancellationToken::Ptr token,
                             const CaptureSession::AcquisitionCallback& onAcquired) {
    FrameAcquirer acquirer(token);
    HRESULT hr = acquirer.Initialize();
    if (hr == E_ABORT) return "CALLBACK_CANCELLED";
    if (FAILED(hr))    return "CALLBACK_ERROR: Desktop duplication init failed";

    if (token && token->IsCancelled()) return "CALLBACK_CANCELLED";

    DirectX::ScratchImage img;
    hr = acquirer.AcquireFrame(img);
    if (hr == E_ABORT) return "CALLBACK_CANCELLED";
    if (FAILED(hr))    return "CALLBACK_ERROR: AcquireFrame failed";

    if (token && token->IsCancelled()) return "CALLBACK_CANCELLED";

    // Frame acquired — restore UI before encoding so DDS/GIF compression
    // time does not keep the HUD suppressed.
    if (onAcquired) onAcquired();

    // Read the Skyrim camera FOV at capture time and derive a
    // 35mm-equivalent focal length for EXIF metadata.  This lets
    // PhotoFileMerge V2 consume the screenshots directly for
    // panorama stitching without manual parameter entry.
    //
    // IMPORTANT: GameCamera::GetHorizontalFOVDegrees() reads
    // RE::PlayerCamera which is a game-thread object.  The worker
    // thread runs concurrently, but PlayerCamera's FOV fields are
    // simple floats set by the console/INI and are not mutated
    // during a capture — they are effectively read-only snapshots.
    //   The read is safe: no virtual dispatch, no node traversal,
    // just two float loads from a singleton pointer.
    double fovDeg = GameCamera::GetHorizontalFOVDegrees();
    double focal35 = GameCamera::FocalLength35mmFromFOV(fovDeg);

    if (focal35 > 0.0) {
        logger::info("RunStill: Skyrim FOV={:.1f}° -> 35mm equiv={:.1f}mm",
                     fovDeg, focal35);
    } else {
        logger::debug("RunStill: FOV unavailable, no EXIF metadata will be written");
    }

    // Build EXIF metadata for the encoder.
    const DirectX::Image* srcImg = img.GetImage(0, 0, 0);
    CaptureExif exif;
    exif.focalLength35mm = focal35;
    exif.fovDegrees      = fovDeg;
    exif.imageWidth      = srcImg ? static_cast<int>(srcImg->width)  : 0;
    exif.imageHeight     = srcImg ? static_cast<int>(srcImg->height) : 0;
    exif.cameraMake      = "Skyrim Engine";
    exif.cameraModel     = "PrintScreen V4";
    exif.software        = "PrintScreen V4";

    std::wstring outPath = MakeOutputPath(req.outputDir, req.format);
    StillEncoder enc(req);
    EncodeResult er = enc.Encode(img, outPath, token, exif);

    if (!er.success) {
        return (er.error == "Cancelled") ? "CALLBACK_CANCELLED"
                                         : "CALLBACK_ERROR: " + er.error;
    }

    std::error_code ec;
    auto sz = std::filesystem::file_size(outPath, ec);
    if (ec || sz == 0) return "CALLBACK_ERROR: Output file empty or missing";

    logger::info("CaptureSession: saved {} ({} bytes)", util::wstring_to_utf8(outPath), sz);
    return "CALLBACK_SUCCESS";
}

// ---- Run one video capture (H264) — zero-copy GPU path ----
//
// Strategy:
//   1. Acquire first frame via AcquireFrameGPU to get desktop dimensions.
//   2. Compute target resolution from req.targetResolution (letterbox preserve aspect).
//   3. If target != native, create GPUScaler for GPU bilinear downscale.
//   4. Init VideoCapture with target resolution dimensions.
//   5. Acquire desktop texture each frame.
//      - If scaling needed: render via GPUScaler to outputTexture_
//        then encode outputTexture_.
//      - If no scaling: encode desktopTexture_ directly.
//   6. On finalize: flush encoder and write moov atom.

static std::string RunVideo(const CaptureRequest& req, CancellationToken::Ptr token) {
    using namespace Printscreen;

    const float duration  = ClampDurationForType(ImageFormat::H264, req.videoDuration);
    const int   fpsInt    = std::clamp(req.videoFrameRate, 1, 120);  // guards /0 below
    const double fps      = static_cast<double>(fpsInt);
    const int totalFrames = std::max(1, static_cast<int>(std::round(duration * fps)));

    logger::info("RunVideo: {} frames at {} fps, duration={:.1f}s, resolution={}",
                 totalFrames, req.videoFrameRate, duration, req.targetResolution);

    // ---- Set up frame acquirer ----
    FrameAcquirer acquirer(token);
    HRESULT hr = acquirer.Initialize();
    if (hr == E_ABORT) return "CALLBACK_CANCELLED";
    if (FAILED(hr))    return "CALLBACK_ERROR: Desktop duplication init failed";
    if (token && token->IsCancelled()) return "CALLBACK_CANCELLED";

    // ---- Acquire first frame to get desktop dimensions ----
    // The desktop duplication API may return WAIT_TIMEOUT on the first call
    // if no desktop update has occurred recently. Retry with a short sleep.
    ID3D11Texture2D* desktopTex = nullptr;
    uint32_t desktopW = 0, desktopH = 0;

    constexpr int kFirstFrameRetries = 10;
    for (int i = 0; i < kFirstFrameRetries; ++i) {
        hr = acquirer.AcquireFrameGPU(desktopTex, desktopW, desktopH);
        if (SUCCEEDED(hr) && desktopTex) break;
        if (hr == E_ABORT) return "CALLBACK_CANCELLED";
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            logger::debug("RunVideo: first frame not ready, retry {}/{}", i + 1, kFirstFrameRetries);
            Sleep(50);
            continue;
        }
        // Any other error is fatal
        break;
    }
    if (FAILED(hr) || !desktopTex) {
        logger::error("RunVideo: AcquireFrameGPU failed on first frame after retries (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return "CALLBACK_ERROR: Failed to acquire first frame";
    }

    // ---- Resolve target resolution ----
    uint32_t targetW = desktopW, targetH = desktopH;
    bool needsScaling = ResolveTargetResolution(req.targetResolution, desktopW, desktopH, targetW, targetH);
    if (needsScaling) {
        logger::info("RunVideo: scaling {}x{} -> {}x{} (letterbox)",
                     desktopW, desktopH, targetW, targetH);
    } else {
        logger::info("RunVideo: native {}x{}", desktopW, desktopH);
    }

    // Build output path and VideoCapture config ----
    std::wstring outPath = MakeOutputPath(req.outputDir, ImageFormat::H264);
    VideoCaptureConfig cfg = BuildVideoConfig(req, targetW, targetH);
    cfg.outputPath = std::filesystem::path(outPath);

    // ---- Init VideoCapture ----
    Printscreen::VideoCapture videoCap;
    if (!videoCap.Initialize(acquirer.GetDevice(), cfg)) {
        acquirer.ReleaseDesktopFrame();
        return "CALLBACK_ERROR: VideoCapture initialization failed";
    }

    // ---- Create GPU scaler if downscaling ----
    std::unique_ptr<GPUScaler> scaler;
    if (needsScaling) {
        scaler = std::make_unique<GPUScaler>(acquirer.GetDevice(),
                                              desktopW, desktopH, targetW, targetH);
        if (!scaler->IsValid()) {
            videoCap.Abort();
            acquirer.ReleaseDesktopFrame();
            return "CALLBACK_ERROR: GPUScaler initialization failed";
        }
    }

    // ---- Texture pool ----
    // The desktop-duplication surface and the GPUScaler output are both single,
    // reused textures. The previous code pushed those raw pointers into the
    // queue, so every queued entry aliased the SAME texture — one the producer
    // overwrites on the very next frame and (for the duplication surface) one
    // that is invalid after ReleaseFrame(). The encoder was racing the producer
    // for the same memory. Every frame is now copied into its own pool slot
    // before being queued; the pool is larger than the queue so a slot is not
    // rewritten while an entry referencing it can still be in flight.
    constexpr std::uint32_t kQueueCapacity = 12;
    constexpr std::uint32_t kPoolSize      = kQueueCapacity + 4;

    DXGI_FORMAT poolFormat = DXGI_FORMAT_B8G8R8A8_UNORM;  // GPUScaler output format
    if (!scaler) {
        D3D11_TEXTURE2D_DESC firstDesc{};
        desktopTex->GetDesc(&firstDesc);
        poolFormat = firstDesc.Format;  // CopyResource requires matching formats
    }

    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> texturePool(kPoolSize);
    {
        D3D11_TEXTURE2D_DESC pd{};
        pd.Width      = targetW;
        pd.Height     = targetH;
        pd.MipLevels  = 1;
        pd.ArraySize  = 1;
        pd.Format     = poolFormat;
        pd.SampleDesc = { 1, 0 };
        pd.Usage      = D3D11_USAGE_DEFAULT;
        pd.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        for (auto& t : texturePool) {
            HRESULT phr = acquirer.GetDevice()->CreateTexture2D(&pd, nullptr, &t);
            if (FAILED(phr)) {
                logger::error("RunVideo: texture pool allocation failed (hr=0x{:08x})",
                              static_cast<unsigned>(phr));
                videoCap.Abort();
                acquirer.ReleaseDesktopFrame();
                return "CALLBACK_ERROR: Frame pool allocation failed";
            }
        }
    }

    std::uint32_t poolIndex = 0;
    auto copyToPool = [&](ID3D11Texture2D* src) -> ID3D11Texture2D* {
        ID3D11Texture2D* dst = texturePool[poolIndex % kPoolSize].Get();
        ++poolIndex;
        acquirer.GetContext()->CopyResource(dst, src);
        return dst;
    };

    // ---- Timing: use QueryPerformanceCounter for wall-clock frame timing ----
    LARGE_INTEGER freq, startQpc;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&startQpc);
    const double qpcToHns = 10'000'000.0 / static_cast<double>(freq.QuadPart);
    const LONGLONG frameDurationHns = kHnsPerSecond / static_cast<LONGLONG>(fpsInt);

    // ---- Create frame queue for asynchronous producer/consumer ----
    // Capacity of 12 frames provides ~200ms of decoupling at 60fps,
    // smoothing out encoder stalls and GPU scheduling jitter without
    // adding excessive latency.
    FrameQueue frameQueue(kQueueCapacity);
    std::atomic<bool> producerDone{ false };
    std::string producerError;
    std::string consumerError;
    std::mutex errorMutex;

    // ---- Queue frame 0 ----
    // The first frame was acquired above to discover the desktop dimensions.
    // Previously it was silently dropped: the producer started at f = 1 and its
    // first AcquireFrameGPU released frame 0 un-encoded. Copy it into the pool
    // and queue it so the video starts at t = 0 — this also means a 1-frame
    // capture no longer finalizes an empty file.
    {
        ID3D11Texture2D* encodeSrc = desktopTex;
        if (scaler) {
            encodeSrc = scaler->Scale(desktopTex, acquirer.GetContext());
            if (!encodeSrc) {
                videoCap.Abort();
                acquirer.ReleaseDesktopFrame();
                return "CALLBACK_ERROR: GPUScaler::Scale failed on first frame";
            }
        }
        ID3D11Texture2D* pooled = copyToPool(encodeSrc);
        acquirer.ReleaseDesktopFrame();  // copy taken — duplication frame no longer needed
        if (!frameQueue.Push(pooled, 0, 0 /* t = 0 */)) {
            videoCap.Abort();
            return "CALLBACK_ERROR: Frame queue rejected first frame";
        }
    }

    // ---- Producer thread: acquire frames and push to queue ----
    std::thread producerThread([&]() {
        try {
            for (int f = 1; f < totalFrames; ++f) {
                if (token && token->IsCancelled()) {
                    frameQueue.Stop();
                    return;
                }

                // High-precision pacing for acquisition
                LARGE_INTEGER nowQpc;
                QueryPerformanceCounter(&nowQpc);
                LONGLONG elapsedHns = static_cast<LONGLONG>(
                    static_cast<double>(nowQpc.QuadPart - startQpc.QuadPart) * qpcToHns);
                LONGLONG targetTimeHns = static_cast<LONGLONG>(
                    static_cast<double>(f) * frameDurationHns);
                LONGLONG remainingHns = targetTimeHns - elapsedHns;

                // Frame-drop recovery: if we're more than one frame behind
                // schedule, skip this frame to resync and prevent a cascade
                // of late frames with incorrect timestamps.
                if (elapsedHns > targetTimeHns + frameDurationHns) {
                    logger::warn("RunVideo: frame {} dropped (behind by {} ms)",
                                 f, (elapsedHns - targetTimeHns) / 10000);
                    acquirer.ReleaseDesktopFrame(); // Release previous if held
                    continue; // Skip to next frame
                }

                // Pacing: sleep until target time instead of spin-waiting.
                // Sleep(1) precision (~1ms) is sufficient; spin-waiting burns
                // CPU cycles that the encoder thread needs.
                if (remainingHns > 0) {
                    auto waitMs = static_cast<DWORD>(remainingHns / 10000);
                    if (waitMs > 0) {
                        Sleep(waitMs);
                    }
                    // No spin-wait for sub-millisecond remainder — the cost
                    // in CPU time exceeds the benefit in timing precision.
                }

                // Acquire next desktop frame
                ID3D11Texture2D* desktopTexLocal = nullptr;
                uint32_t dw = 0, dh = 0;
                HRESULT hrLocal = acquirer.AcquireFrameGPU(desktopTexLocal, dw, dh);
                if (hrLocal == DXGI_ERROR_WAIT_TIMEOUT) {
                    // No new frame ready — let the pacing loop handle the gap.
                    // The frame-drop recovery above will skip if we're behind.
                    logger::debug("RunVideo: frame {} not ready yet, waiting", f);
                    Sleep(1); // avoid a hot spin when behind schedule with no frame ready
                    f--; // Retry this frame index on next iteration
                    continue;
                }
                if (hrLocal == E_ABORT || (token && token->IsCancelled())) {
                    frameQueue.Stop();
                    return;
                }
                if (FAILED(hrLocal) || !desktopTexLocal) {
                    logger::error("RunVideo: AcquireFrameGPU failed at frame {} (hr=0x{:08x})",
                                  f, static_cast<unsigned>(hrLocal));
                    {
                        std::lock_guard<std::mutex> lk(errorMutex);
                        producerError = "CALLBACK_ERROR: AcquireFrameGPU failed";
                    }
                    frameQueue.Stop();
                    return;
                }

                // Capture-time presentation timestamp (100ns units). Recorded
                // here — at acquisition — rather than in the consumer at encode
                // time, so queue latency does not shift frame timing.
                QueryPerformanceCounter(&nowQpc);
                const LONGLONG captureTimeHns = static_cast<LONGLONG>(
                    static_cast<double>(nowQpc.QuadPart - startQpc.QuadPart) * qpcToHns);

                // Scale if needed
                ID3D11Texture2D* encodeTexLocal = desktopTexLocal;
                if (scaler) {
                    encodeTexLocal = scaler->Scale(desktopTexLocal, acquirer.GetContext());
                    if (!encodeTexLocal) {
                        std::lock_guard<std::mutex> lk(errorMutex);
                        producerError = "CALLBACK_ERROR: GPUScaler::Scale failed";
                        frameQueue.Stop();
                        return;
                    }
                }

                // Copy into a pool slot so the encoder owns a stable texture,
                // then release the duplication frame immediately.
                ID3D11Texture2D* pooledTex = copyToPool(encodeTexLocal);
                acquirer.ReleaseDesktopFrame();

                // Push to queue (blocks if full)
                if (!frameQueue.Push(pooledTex, static_cast<std::uint32_t>(f), captureTimeHns)) {
                    return; // Queue stopped
                }
            }
        } catch (const Cancelled&) {
            // Cooperative cancellation (thrown by FrameAcquirer::CheckCancel).
            // Must NOT be treated as an error — fall through so the final
            // token->IsCancelled() check reports CALLBACK_CANCELLED. The old
            // generic catch below turned every user cancel into
            // "CALLBACK_ERROR: Producer thread exception: Cancelled".
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(errorMutex);
            producerError = std::string("CALLBACK_ERROR: Producer thread exception: ") + e.what();
            frameQueue.Stop();
        }
        producerDone.store(true);
        frameQueue.Stop(); // Signal consumer we're done
    });

    // ---- Consumer: encode frames from queue ----
    Microsoft::WRL::ComPtr<ID3D11Texture2D> encodeTex;
    std::uint32_t frameIndex = 0;
    LONGLONG captureTimeHns = 0;
    LONGLONG prevSampleTimeHns = -1;

    while (true) {
        bool gotFrame = frameQueue.Pop(encodeTex, frameIndex, captureTimeHns);
        if (!gotFrame) {
            // Queue empty and stopped
            if (producerDone.load() && frameQueue.IsEmpty()) {
                break; // Normal completion
            }
            // Stopped prematurely - check for errors
            std::lock_guard<std::mutex> lk(errorMutex);
            if (!producerError.empty()) {
                consumerError = producerError;
            }
            break;
        }

        // Use the capture-time timestamp recorded by the producer. The old
        // code sampled the clock HERE, at encode time, so timestamps were
        // inflated by however long the frame sat in the queue. Strict
        // monotonicity is enforced for the sink writer.
        LONGLONG sampleTimeHns = std::max(captureTimeHns, prevSampleTimeHns + 1);
        prevSampleTimeHns = sampleTimeHns;
        LONGLONG sampleDurationHns = frameDurationHns;

        if (!videoCap.EncodeFrame(encodeTex.Get(), frameIndex, sampleTimeHns, sampleDurationHns)) {
            std::lock_guard<std::mutex> lk(errorMutex);
            consumerError = "CALLBACK_ERROR: VideoCapture::EncodeFrame failed";
            frameQueue.Stop();
            break;
        }
    }

    // Wait for producer to finish
    if (producerThread.joinable()) {
        producerThread.join();
    }

    // Check for errors
    {
        std::lock_guard<std::mutex> lk(errorMutex);
        if (!consumerError.empty()) {
            videoCap.Abort();
            return consumerError;
        }
        if (!producerError.empty()) {
            videoCap.Abort();
            return producerError;
        }
    }

    if (token && token->IsCancelled()) {
        videoCap.Abort();
        return "CALLBACK_CANCELLED";
    }

    // ---- Finalize ----
    if (!videoCap.Finalize()) {
        return "CALLBACK_ERROR: VideoCapture::Finalize failed";
    }

    std::error_code ec;
    auto sz = std::filesystem::file_size(outPath, ec);
    if (ec || sz == 0) return "CALLBACK_ERROR: Output file empty or missing";

    logger::info("RunVideo: saved {} ({} bytes)", util::wstring_to_utf8(outPath), sz);
    return "CALLBACK_SUCCESS";
}

// ---- Resolve target resolution from enum value ----
//
// targetResolution: 0=Native, 1=720p, 2=1080p, 3=1440p, 4=4K
// outW/outH are set to the target dimensions (desktop dims if Native).
// Returns true if scaling is needed (target != desktop), false otherwise.

static bool ResolveTargetResolution(int targetResolution,
                                     uint32_t desktopW, uint32_t desktopH,
                                     uint32_t& outW, uint32_t& outH) {
    // If native, keep full desktop dimensions
    if (targetResolution == 0) {
        outW = desktopW;
        outH = desktopH;
        return false;  // no scaling (previous expression compared outW to itself)
    }

    // Target height based on resolution preset
    uint32_t targetH = 0;
    switch (targetResolution) {
        case 1: targetH =  720; break;   // 720p
        case 2: targetH = 1080; break;   // 1080p
        case 3: targetH = 1440; break;   // 1440p
        case 4: targetH = 2160; break;   // 4K
        default: targetH = 1080; break;
    }

    // Clamp: don't upscale if desktop is smaller than target
    if (desktopH <= targetH) {
        outW = desktopW;
        outH = desktopH;
        logger::info("ResolveTargetResolution: desktop {}x{} is smaller than target {}p, using native",
                     desktopW, desktopH, targetH);
        return false;  // no scaling
    }

    // Compute width to preserve aspect ratio (letterbox: fit to height)
    // aspect = desktopW / desktopH
    // targetW = round(targetH * aspect)
    outH = targetH;
    outW = static_cast<uint32_t>(std::round(static_cast<double>(targetH) *
                                             static_cast<double>(desktopW) /
                                             static_cast<double>(desktopH)));

    // Ensure even dimensions (MF requires this for video)
    outW = (outW + 1) & ~1u;
    outH = (outH + 1) & ~1u;

    logger::info("ResolveTargetResolution: {}x{} (desktop) -> {}x{} (target)",
                 desktopW, desktopH, outW, outH);

    return (outW != desktopW || outH != desktopH);
}

// Build a VideoCaptureConfig from the video fields in CaptureRequest.
static Printscreen::VideoCaptureConfig BuildVideoConfig(
    const CaptureRequest& req, uint32_t dstWidth, uint32_t dstHeight)
{
    using namespace Printscreen;
    VideoCaptureConfig cfg;

    // Use the resolved target dimensions (may be smaller than desktop)
    cfg.width     = dstWidth;
    cfg.height    = dstHeight;
    cfg.frameRate = static_cast<std::uint32_t>(req.videoFrameRate);

    // Quality preset to bitrate mapping — scales with resolution and fps
    switch (req.qualityPreset) {
        case 0:  cfg.bitrateKbps = 4000;  break;  // Low
        case 1:  cfg.bitrateKbps = 8000;  break;  // Medium
        case 2:  cfg.bitrateKbps = 16000; break;  // High
        case 3:  cfg.bitrateKbps = 35000; break;  // Very High
        case 4:  cfg.bitrateKbps = static_cast<std::uint32_t>(req.videoBitrateKbps); break;  // Custom
        default: cfg.bitrateKbps = 16000; break;
    }

    cfg.keyframeIntervalSec = static_cast<std::uint32_t>(std::max(1.0f, req.keyframeIntervalSec));

    // Encoder preference
    switch (req.encoderPreference) {
        case 1:  cfg.encoderPreference = EncoderPreference::PreferHardware; break;
        case 2:  cfg.encoderPreference = EncoderPreference::ForceSoftware;  break;
        default: cfg.encoderPreference = EncoderPreference::Auto;           break;
    }

    // Rate control
    switch (req.rateControl) {
        case 0:  cfg.rateControl = RateControlMode::CBR; break;
        case 2:  cfg.rateControl = RateControlMode::CQP; break;
        default: cfg.rateControl = RateControlMode::VBR; break;
    }

    // Container -- MKV falls back to MP4 inside VideoCapture
    cfg.container = (req.videoContainer == 1)
        ? VideoContainer::MKV
        : VideoContainer::MP4;

    return cfg;
}

// ---- Clamp duration per image type ----
static float ClampDurationForType(ImageFormat fmt, float duration) {
    switch (fmt) {
        case ImageFormat::AGIF:
        case ImageFormat::APNG:
            return std::clamp(duration, 0.1f, 15.0f);
        case ImageFormat::H264:
            return std::clamp(duration, 1.0f, 120.0f);
        default:
            return duration;
    }
}

} // namespace

// ============================================================
// CaptureSession singleton
// ============================================================
CaptureSession& CaptureSession::GetSingleton() {
    static CaptureSession instance;
    return instance;
}

CaptureSession::~CaptureSession() {
    if (token_) token_->Cancel();
    JoinWorker();
}

void CaptureSession::JoinWorker() {
    std::lock_guard jl(joinMutex_);
    if (workerThread_.joinable()) workerThread_.join();
}

CancellationToken::Ptr CaptureSession::GetToken() const {
    std::lock_guard lock(mutex_);
    return token_;
}

bool CaptureSession::IsIdle() const {
    std::lock_guard lock(mutex_);
    return state_ == State::Idle || state_ == State::Done;
}

// ============================================================
// Start
// ============================================================
CaptureSession::StartResult CaptureSession::Start(CaptureRequest request,
                                                   CompletionCallback  onComplete,
                                                   AcquisitionCallback onAcquisitionComplete) {
    std::unique_lock lock(mutex_);

    // ── Self-rectifying state handling ─────────────────────────────
    // If a previous capture finished but its result was never consumed,
    // clear the stale Done state and become Idle so we can start fresh.
    if (state_ == State::Done) {
        logger::info("CaptureSession::Start: auto-consuming stale Done state");
        state_  = State::Idle;
        result_ = "Ready";
    }

    // If a capture is currently active, cancel it and return.
    // The caller (Papyrus) will receive BusyCancelled and must call Start
    // again later if they want to begin a new capture.
    if (state_ == State::Starting || state_ == State::Running) {
        logger::info("CaptureSession::Start: cancelling active capture (second call)");
        if (token_) token_->Cancel();
        lock.unlock();                       // release mutex while joining
        JoinWorker();
        lock.lock();                         // re-acquire for state reset
        // Only reset if the state is the joined worker's Done — if another
        // Start() slipped in while the mutex was released, don't clobber
        // the new session's state/token.
        if (state_ == State::Done) {
            state_  = State::Idle;
            result_ = "Ready";
            token_.reset();
        }
        return StartResult::BusyCancelled;
    }

    // Reap the previous worker thread. This must happen WITHOUT holding
    // mutex_: if a ForceReset() (MYReset / game reload) forced the state to
    // Idle while the old worker was still running, that worker's SetResult()
    // needs mutex_ to finish — joining it here while holding the lock was a
    // guaranteed deadlock that froze the game thread.
    lock.unlock();
    JoinWorker();
    lock.lock();

    // Re-validate: another Start() may have slipped in while unlocked, or the
    // joined worker may have just published its (stale) Done result.
    if (state_ == State::Done) {
        state_  = State::Idle;
        result_ = "Ready";
    }
    if (state_ != State::Idle) {
        logger::info("CaptureSession::Start: lost race to another Start() while joining");
        return StartResult::AlreadyRunning;
    }

    token_                 = CancellationToken::Make();
    state_                 = State::Starting;
    result_                = "Starting";
    onComplete_            = std::move(onComplete);           // set BEFORE thread starts
    onAcquisitionComplete_ = std::move(onAcquisitionComplete);

    {
        // joinMutex_ is taken *after* mutex_ here; JoinWorker() takes it
        // without mutex_ — consistent ordering, no deadlock. This prevents a
        // concurrent JoinWorker() from touching the thread object mid-assign.
        std::lock_guard jl(joinMutex_);
        workerThread_ = std::thread([this, req = std::move(request)]() mutable {
            WorkerThread(std::move(req));
        });
    }
    return StartResult::Accepted;
}

void CaptureSession::RequestCancel() {
    CancellationToken::Ptr tok;
    { std::lock_guard lock(mutex_); tok = token_; }
    if (tok) tok->Cancel();
}

void CaptureSession::ForceReset() {
    RequestCancel();
    std::lock_guard lock(mutex_);
    if (state_ == State::Starting || state_ == State::Running) {
        // The worker is still alive. Do NOT force Idle here: doing so let a
        // subsequent Start() treat the session as free while the old worker
        // still owned the thread object and would later clobber state via
        // SetResult. The cancel above makes it finish quickly; its Done state
        // is auto-consumed by Start()/GetResult().
        logger::info("CaptureSession::ForceReset: worker still active — cancel signalled, state left for auto-consume");
        return;
    }
    state_  = State::Idle;
    result_ = "Ready";
    logger::info("CaptureSession::ForceReset");
}

std::string CaptureSession::GetResult() {
    std::lock_guard lock(mutex_);
    if (state_ == State::Done) {
        std::string r = result_;
        state_  = State::Idle;
        result_ = "Ready";
        return r;
    }
    return result_;
}

void CaptureSession::SetResult(std::string result) {
    CompletionCallback cb;
    {
        std::lock_guard lock(mutex_);
        result_ = result;
        state_  = State::Done;
        cb      = onComplete_;
    }
    if (cb) cb(result);
}

// ============================================================
// WorkerThread
// ============================================================
void CaptureSession::WorkerThread(CaptureRequest req) {
    // Take local copies of the token and callback under the mutex. The worker
    // previously read the members directly with no lock while the game thread
    // could reset them (Start's BusyCancelled path) — a data race on shared_ptr
    // and std::function.
    CancellationToken::Ptr  token;
    AcquisitionCallback     onAcquired;
    {
        std::lock_guard lock(mutex_);
        state_     = State::Running;
        result_    = "Running";
        token      = token_;
        onAcquired = onAcquisitionComplete_;
    }

    logger::info("CaptureSession::WorkerThread: format={}", static_cast<int>(req.format));

    std::string completionResult;
    try {
        if (token) token->ThrowIfCancelled("worker start");

        const bool isAnimated = (req.format == ImageFormat::AGIF ||
                                  req.format == ImageFormat::APNG);
        const bool isVideo = (req.format == ImageFormat::H264);

        if (isVideo) {
            // H264 interleaves acquisition and encoding per-frame — no mid-capture restore.
            completionResult = RunVideo(req, token);
        } else if (isAnimated) {
            completionResult = RunAnimated(req, token, onAcquired);
        } else {
            completionResult = RunStill(req, token, onAcquired);
        }
    } catch (const Cancelled&) {
        completionResult = "CALLBACK_CANCELLED";
    } catch (const std::exception& e) {
        completionResult = std::string("CALLBACK_ERROR: ") + e.what();
        logger::error("CaptureSession worker: {}", e.what());
    } catch (...) {
        completionResult = "CALLBACK_ERROR: Unknown exception";
        logger::error("CaptureSession worker: unknown exception");
    }

    logger::info("CaptureSession worker done: {}", completionResult);
    SetResult(completionResult);
}