#include "PCH.h"
#include "FrameAcquirer.h"
#include "stringutils.h"

FrameAcquirer::FrameAcquirer(CancellationToken::Ptr token)
    : token_(std::move(token)) {}

FrameAcquirer::~FrameAcquirer() {
    // Release any duplication frame still held so the destructor of
    // duplication_ doesn't tear down with an outstanding frame.
    ReleasePendingFrame();
}

void FrameAcquirer::CheckCancel(const char* stage) const {
    if (token_) token_->ThrowIfCancelled(stage);
}

// ============================================================
// Initialize
// ============================================================
HRESULT FrameAcquirer::Initialize() {
    logger::info("FrameAcquirer: initializing desktop duplication");

    CheckCancel("FrameAcquirer::Initialize start");

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        &device_, &featureLevel, &context_);
    if (FAILED(hr)) {
        logger::error("FrameAcquirer: D3D11CreateDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }

    // Enable multithread protection on the device — required by VideoCapture
    // so that the MF sink writer can safely share the D3D11 device.
    {
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device_.As(&mt))) {
            mt->SetMultithreadProtected(TRUE);
            logger::info("FrameAcquirer: D3D11 multithread protection enabled");
        } else {
            logger::warn("FrameAcquirer: ID3D10Multithread not available — video capture may fail");
        }
    }

    CheckCancel("FrameAcquirer::Initialize after D3D device");

    Microsoft::WRL::ComPtr<IDXGIDevice>  dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) { logger::error("FrameAcquirer: IDXGIDevice query failed"); return hr; }

    CheckCancel("FrameAcquirer::Initialize after DXGI device");

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) { logger::error("FrameAcquirer: GetAdapter failed"); return hr; }

    CheckCancel("FrameAcquirer::Initialize after adapter");

    Microsoft::WRL::ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) { logger::error("FrameAcquirer: EnumOutputs(0) failed"); return hr; }

    CheckCancel("FrameAcquirer::Initialize after output");

    Microsoft::WRL::ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) { logger::error("FrameAcquirer: IDXGIOutput1 query failed"); return hr; }

    hr = dxgiOutput1->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        logger::error("FrameAcquirer: DuplicateOutput failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return hr;
    }

    CheckCancel("FrameAcquirer::Initialize after duplication create");

    initialized_ = true;
    logger::info("FrameAcquirer: desktop duplication ready");
    return S_OK;
}

// ============================================================
// AcquireFrameGPU — GPU zero-copy path for video
// ============================================================
HRESULT FrameAcquirer::AcquireFrameGPU(ID3D11Texture2D*& outTexture,
                                        uint32_t& outWidth, uint32_t& outHeight) {
    if (!initialized_) {
        logger::error("FrameAcquirer::AcquireFrameGPU called before Initialize()");
        outTexture = nullptr;
        return E_FAIL;
    }

    CheckCancel("FrameAcquirer::AcquireFrameGPU start");

    // Release any previous frame before acquiring the next one.
    // This is required by DuplicateOutput — you must release the frame
    // before acquiring the next, otherwise AcquireNextFrame returns
    // DXGI_ERROR_FRAME_STATS_DISCONTINUITY.
    ReleasePendingFrame();

    CheckCancel("FrameAcquirer::AcquireFrameGPU after release");

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    constexpr int kMaxRetries = 2;
    HRESULT hr = DXGI_ERROR_WAIT_TIMEOUT;
    for (int i = 0; i < kMaxRetries; ++i) {
        CheckCancel(("FrameAcquirer::AcquireFrameGPU retry " + std::to_string(i)).c_str());
        // Use 0ms timeout for non-blocking acquire. The caller's timing loop
        // (CaptureSession::RunVideo) handles pacing externally, so blocking
        // here only adds latency and prevents frame-drop recovery.
        hr = duplication_->AcquireNextFrame(0, &frameInfo, &desktopResource);
        if (SUCCEEDED(hr)) break;
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame yet. Caller will pace and retry at the correct time.
            logger::debug("FrameAcquirer: no frame ready (retry {}/{})", i + 1, kMaxRetries);
            return hr;
        }
        logger::error("FrameAcquirer::AcquireFrameGPU: AcquireNextFrame failed: 0x{:08X}",
                      static_cast<uint32_t>(hr));
        outTexture = nullptr;
        return hr;
    }
    // If we get here with WAIT_TIMEOUT, the caller should handle pacing
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        outTexture = nullptr;
        return hr;
    }

    hasAcquiredFrame_ = true;

    CheckCancel("FrameAcquirer::AcquireFrameGPU after acquire");

    // Get the raw GPU texture from the DXGI resource
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    hr = desktopResource.As(&tex);
    if (FAILED(hr)) {
        ReleasePendingFrame();
        logger::error("FrameAcquirer::AcquireFrameGPU: texture QI failed");
        outTexture = nullptr;
        return hr;
    }

    // Get dimensions for caller
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    outWidth  = desc.Width;
    outHeight = desc.Height;

    // Keep the desktop texture alive (valid until next AcquireFrameGPU or ReleaseDesktopFrame)
    desktopTexture_ = tex;

    outTexture = desktopTexture_.Get();
    logger::debug("FrameAcquirer::AcquireFrameGPU: {}x{}", outWidth, outHeight);
    return S_OK;
}

// ============================================================
// ReleaseDesktopFrame
// ============================================================
void FrameAcquirer::ReleaseDesktopFrame() noexcept {
    ReleasePendingFrame();
}

// ============================================================
// ReleasePendingFrame — internal
// ============================================================
void FrameAcquirer::ReleasePendingFrame() noexcept {
    if (duplication_ && hasAcquiredFrame_) {
        duplication_->ReleaseFrame();
        hasAcquiredFrame_ = false;
    }
    // Note: we do NOT release desktopTexture_ here — the caller may still
    // be using it for GPU encoding. It gets overwritten on the next
    // AcquireFrameGPU call.
}

// ============================================================
// AcquireFrame — CPU staging path for stills/animated
// ============================================================
HRESULT FrameAcquirer::AcquireFrame(DirectX::ScratchImage& outImage) {
    if (!initialized_) {
        logger::error("FrameAcquirer::AcquireFrame called before Initialize()");
        return E_FAIL;
    }

    CheckCancel("FrameAcquirer::AcquireFrame start");

    // Release any pending desktop frame first
    ReleasePendingFrame();

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    constexpr int kMaxRetries = 5;
    HRESULT hr = DXGI_ERROR_WAIT_TIMEOUT;
    for (int i = 0; i < kMaxRetries; ++i) {
        CheckCancel(("FrameAcquirer::AcquireFrame retry " + std::to_string(i)).c_str());
        // Use 100ms timeout for faster recovery when desktop is static
        hr = duplication_->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (SUCCEEDED(hr)) break;
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            logger::debug("FrameAcquirer: timeout, retry {}/{}", i + 1, kMaxRetries);
            std::this_thread::yield();
            continue;
        }
        logger::error("FrameAcquirer::AcquireFrame: AcquireNextFrame failed: 0x{:08X}",
                       static_cast<uint32_t>(hr));
        return hr;
    }
    if (FAILED(hr)) {
        logger::error("FrameAcquirer::AcquireFrame: frame acquisition failed after {} retries",
                       kMaxRetries);
        return hr;
    }

    hasAcquiredFrame_ = true;

    CheckCancel("FrameAcquirer::AcquireFrame after acquire");

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        ReleasePendingFrame();
        logger::error("FrameAcquirer::AcquireFrame: texture QI failed");
        return hr;
    }

    // Keep raw GPU texture (for zero-copy video path users)
    lastFrameTexture_ = desktopTexture;

    D3D11_TEXTURE2D_DESC desc;
    desktopTexture->GetDesc(&desc);

    CheckCancel("FrameAcquirer::AcquireFrame before staging texture");

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage          = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags      = 0;
    stagingDesc.MiscFlags      = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    hr = device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        ReleasePendingFrame();
        logger::error("FrameAcquirer::AcquireFrame: CreateTexture2D (staging) failed");
        return hr;
    }

    CheckCancel("FrameAcquirer::AcquireFrame before copy");

    context_->CopyResource(stagingTexture.Get(), desktopTexture.Get());

    CheckCancel("FrameAcquirer::AcquireFrame before map");

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        ReleasePendingFrame();
        logger::error("FrameAcquirer::AcquireFrame: texture Map failed");
        return hr;
    }

    hr = outImage.Initialize2D(desc.Format, desc.Width, desc.Height, 1, 1);
    if (FAILED(hr)) {
        context_->Unmap(stagingTexture.Get(), 0);
        ReleasePendingFrame();
        logger::error("FrameAcquirer::AcquireFrame: ScratchImage::Initialize2D failed");
        return hr;
    }

    CheckCancel("FrameAcquirer::AcquireFrame before pixel copy");

    const DirectX::Image* img = outImage.GetImage(0, 0, 0);
    if (!img) {
        context_->Unmap(stagingTexture.Get(), 0);
        ReleasePendingFrame();
        return E_FAIL;
    }

    const auto*  src      = static_cast<const uint8_t*>(mapped.pData);
    uint8_t*     dst      = img->pixels;
    const size_t rowBytes = std::min<size_t>(mapped.RowPitch, img->rowPitch);

    // Cancellation is checked once before the copy rather than mid-loop:
    // throwing out of the loop would leak the Map and the duplication frame.
    CheckCancel("FrameAcquirer::AcquireFrame pixel copy");

    for (size_t y = 0; y < desc.Height; ++y) {
        std::memcpy(dst + y * img->rowPitch, src + y * mapped.RowPitch, rowBytes);
    }

    context_->Unmap(stagingTexture.Get(), 0);
    ReleasePendingFrame();  // clears hasAcquiredFrame_ — prevents a double
                            // ReleaseFrame() on the next acquire

    logger::debug("FrameAcquirer::AcquireFrame: frame acquired ({}x{})", desc.Width, desc.Height);
    return S_OK;
}