#pragma once
#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>

// ============================================================
// GPUScaler
// Render-quad based bilinear GPU downscale for video encoding.
//
// Output texture is BGRA8_UNORM with D3D11_BIND_SHADER_RESOURCE |
// D3D11_BIND_RENDER_TARGET, suitable for MFCreateDXGISurfaceBuffer.
// ============================================================
class GPUScaler {
public:
    GPUScaler(ID3D11Device* device,
              uint32_t srcWidth, uint32_t srcHeight,
              uint32_t dstWidth, uint32_t dstHeight);
    ~GPUScaler() = default;

    GPUScaler(const GPUScaler&)            = delete;
    GPUScaler& operator=(const GPUScaler&) = delete;

    // Scale input texture to target resolution (letterbox preserving aspect).
    // Returns the output texture (valid until the next Scale() call or destruction).
    ID3D11Texture2D* Scale(ID3D11Texture2D* input, ID3D11DeviceContext* context);

    [[nodiscard]] bool IsValid() const noexcept { return valid_; }
    [[nodiscard]] uint32_t DstWidth()  const noexcept { return dstWidth_;  }
    [[nodiscard]] uint32_t DstHeight() const noexcept { return dstHeight_; }

private:
    bool CreateRenderTarget(ID3D11Device* device);
    bool CreateShaders(ID3D11Device* device);
    bool CreateConstantBuffer(ID3D11Device* device);

    uint32_t srcWidth_  = 0;
    uint32_t srcHeight_ = 0;
    uint32_t dstWidth_  = 0;
    uint32_t dstHeight_ = 0;
    bool     valid_     = false;

    Microsoft::WRL::ComPtr<ID3D11Device>             device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>           outputTexture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    rtv_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>        vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>         ps_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>        sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              cbuffer_;
    Microsoft::WRL::ComPtr<ID3D11Query>               completionQuery_;
};