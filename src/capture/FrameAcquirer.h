#pragma once
#include "CancellationToken.h"
#include <DirectXTex.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// ============================================================
// FrameAcquirer
// Owns the D3D11 device and DXGI desktop-duplication objects.
// Call Initialize() once, then use AcquireFrame (CPU path for stills/animated)
// or AcquireFrameGPU (GPU path for video).
// Non-copyable; move is supported.
// All methods check the cancellation token provided at construction time.
// ============================================================
class FrameAcquirer {
public:
    explicit FrameAcquirer(CancellationToken::Ptr token);
    ~FrameAcquirer();

    FrameAcquirer(const FrameAcquirer&)            = delete;
    FrameAcquirer& operator=(const FrameAcquirer&) = delete;
    FrameAcquirer(FrameAcquirer&&)                 = default;
    FrameAcquirer& operator=(FrameAcquirer&&)      = default;

    // Creates the D3D11 device and DXGI output-duplication interface.
    // Returns S_OK on success, E_ABORT if cancelled, other HRESULTs on error.
    HRESULT Initialize();

    bool IsInitialized() const noexcept { return initialized_; }

    // D3D11 device and context for GPU operations (VideoCapture, GPUScaler).
    ID3D11Device*       GetDevice()  const noexcept { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const noexcept { return context_.Get(); }

    // ------------------------------------------------------------
    // CPU path — for stills and animated (GIF/APNG)
    // ------------------------------------------------------------
    // Captures one frame into outImage (CPU staging copy).
    // Returns S_OK, E_ABORT (cancelled), or an error HRESULT.
    HRESULT AcquireFrame(DirectX::ScratchImage& outImage);

    // ------------------------------------------------------------
    // GPU path — for video (H264 zero-copy encoding)
    // ------------------------------------------------------------
    // Acquires the raw DXGI desktop texture for GPU-only encoding.
    // Does NOT copy to a staging texture — returns the GPU texture directly.
    //
    // The returned texture stays valid until the next AcquireFrameGPU call
    // (auto-releases the previous frame) or ReleaseDesktopFrame().
    //
    // Returns S_OK and sets outTexture/outWidth/outHeight on success.
    // Returns E_ABORT if cancelled.
    // Returns an error HRESULT on failure.
    HRESULT AcquireFrameGPU(ID3D11Texture2D*& outTexture,
                             uint32_t& outWidth, uint32_t& outHeight);

    // Call this to manually release the desktop frame early.
    // Normally you don't need to call this — AcquireFrameGPU calls it
    // automatically at the start of the next call.
    void ReleaseDesktopFrame() noexcept;

private:
    void CheckCancel(const char* stage) const;
    void ReleasePendingFrame() noexcept;

    CancellationToken::Ptr               token_;
    Microsoft::WRL::ComPtr<ID3D11Device>             device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>       context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication>    duplication_;
    bool                                           initialized_{false};

    // The current desktop texture (valid between AcquireFrameGPU and the next
    // call, or between AcquireFrame start and ReleaseFrame). Kept alive so
    // callers can use it for GPU encoding without a copy.
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          desktopTexture_;

    // Legacy: CPU-path scratch image staging texture (used by AcquireFrame).
    // Kept separate from desktopTexture_ so the CPU and GPU paths don't
    // interfere with each other.
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          lastFrameTexture_;

    // Tracks whether we have acquired (and not yet released) a desktop frame.
    // Prevents calling ReleaseFrame() before the first AcquireNextFrame().
    bool                                           hasAcquiredFrame_{false};
};