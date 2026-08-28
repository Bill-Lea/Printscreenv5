// GPUScaler.cpp
// Render-quad based bilinear GPU downscale for video encoding.

#include "PCH.h"
#include "GPUScaler.h"
#include "logger.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace {

// =============================================================================
// Inline HLSL shaders compiled at runtime using D3DCompile
// =============================================================================

// Vertex shader: fullscreen quad from vertex ID (no vertex buffer needed).
// vertexID: 0=BL, 1=BR, 2=TL, 3=TR.
const char* kVertexShader = R"(
    void main(uint vertexID : SV_VertexID,
              out float4 pos : SV_POSITION,
              out float2 uv  : TEXCOORD0)
    {
        float2 corners[4] = {
            float2(-1.0,  1.0),  // TL (vertex 2)
            float2( 1.0,  1.0),  // TR (vertex 3)
            float2(-1.0, -1.0),  // BL (vertex 0)
            float2( 1.0, -1.0)   // BR (vertex 1)
        };
        float2 uvCoords[4] = {
            float2(0.0, 0.0),    // TL
            float2(1.0, 0.0),     // TR
            float2(0.0, 1.0),     // BL
            float2(1.0, 1.0)      // BR
        };
        pos = float4(corners[vertexID], 0.0, 1.0);
        uv  = uvCoords[vertexID];
    }
)";

// Pixel shader: bilinear sample with letterbox aspect-ratio preservation.
// The image is scaled to fit the shorter axis of the destination and centered,
// so black bars appear on the longer axis (letterbox).
const char* kPixelShader = R"(
    Texture2D<float4> inputTexture : register(t0);
    SamplerState linearSampler    : register(s0);

    cbuffer LetterboxParams : register(b0) {
        float srcWidth;
        float srcHeight;
        float dstWidth;
        float dstHeight;
        float offsetX;  // UV offset to center the image (0 = no horizontal letterbox)
        float offsetY;  // UV offset to center the image (0 = no vertical letterbox)
        float scaleX;   // fraction of dstWidth that src occupies (1.0 = full width)
        float scaleY;   // fraction of dstHeight that src occupies (1.0 = full height)
    };

    void main(float4 pos : SV_POSITION,
              float2 uv  : TEXCOORD0,
              out float4 outputColor : SV_Target0)
    {
        // Apply letterbox scale + center offset
        float2 scaledUV = float2(offsetX, offsetY) + uv * float2(scaleX, scaleY);
        scaledUV = saturate(scaledUV);
        outputColor = inputTexture.Sample(linearSampler, scaledUV);
    }
)";

} // namespace

// =============================================================================
// Letterbox constant buffer — shared with pixel shader
// =============================================================================
struct LetterboxCBuffer {
    float srcWidth;
    float srcHeight;
    float dstWidth;
    float dstHeight;
    float offsetX;
    float offsetY;
    float scaleX;
    float scaleY;
};

// =============================================================================
// GPUScaler public
// =============================================================================

GPUScaler::GPUScaler(ID3D11Device* device,
                     uint32_t srcWidth, uint32_t srcHeight,
                     uint32_t dstWidth, uint32_t dstHeight)
    : srcWidth_(srcWidth)
    , srcHeight_(srcHeight)
    , dstWidth_(dstWidth)
    , dstHeight_(dstHeight)
    , device_(device)
{
    if (!device_) {
        logger::error("GPUScaler: null device");
        return;
    }
    if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0) {
        logger::error("GPUScaler: invalid dimensions (src={}x{}, dst={}x{})",
                      srcWidth, srcHeight, dstWidth, dstHeight);
        return;
    }
    if (srcWidth == dstWidth && srcHeight == dstHeight) {
        logger::info("GPUScaler: no scaling needed ({}x{} -> {}x{})",
                     srcWidth, srcHeight, dstWidth, dstHeight);
        valid_ = true;
        return;
    }

    if (!CreateRenderTarget(device)) {
        logger::error("GPUScaler: CreateRenderTarget failed");
        return;
    }
    if (!CreateShaders(device)) {
        logger::error("GPUScaler: CreateShaders failed");
        return;
    }
    if (!CreateConstantBuffer(device)) {
        logger::error("GPUScaler: CreateConstantBuffer failed");
        return;
    }

    // Pre-create event query for non-blocking GPU completion signaling.
    // Reusing the query eliminates per-frame allocation overhead and
    // removes the need for a blocking context->Flush() fallback.
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    HRESULT hr = device->CreateQuery(&queryDesc, &completionQuery_);
    if (FAILED(hr)) {
        logger::warn("GPUScaler: CreateQuery(EVENT) failed (hr=0x{:08x}) — will use Flush fallback",
                      static_cast<unsigned>(hr));
        // Non-fatal: Scale() falls back to Flush() if query is null
    }

    valid_ = true;
    logger::info("GPUScaler: {}x{} -> {}x{} (letterbox)",
                 srcWidth_, srcHeight_, dstWidth_, dstHeight_);
}

ID3D11Texture2D* GPUScaler::Scale(ID3D11Texture2D* input, ID3D11DeviceContext* context) {
    if (!valid_ || !context || !input) {
        logger::error("GPUScaler::Scale: invalid state or null input");
        return nullptr;
    }

    // No-op case
    if (srcWidth_ == dstWidth_ && srcHeight_ == dstHeight_) {
        return input;
    }

    if (!outputTexture_ || !rtv_) {
        logger::error("GPUScaler::Scale: internal resources not ready");
        return nullptr;
    }

    // ---- Compute letterbox parameters ----
    LetterboxCBuffer cbData = {};
    cbData.srcWidth  = static_cast<float>(srcWidth_);
    cbData.srcHeight = static_cast<float>(srcHeight_);
    cbData.dstWidth  = static_cast<float>(dstWidth_);
    cbData.dstHeight = static_cast<float>(dstHeight_);

    float srcAspect = cbData.srcWidth  / cbData.srcHeight;
    float dstAspect = cbData.dstWidth  / cbData.dstHeight;

    if (srcAspect > dstAspect) {
        // Source is wider than destination: fit to width, letterbox top/bottom.
        // Image fills 100% of width; vertical scale = dstAspect/srcAspect.
        cbData.scaleX  = 1.0f;
        cbData.scaleY  = dstAspect / srcAspect;
        cbData.offsetX = 0.0f;
        cbData.offsetY = (1.0f - cbData.scaleY) * 0.5f;  // center vertically
    } else {
        // Source is taller or same: fit to height, letterbox left/right.
        // Image fills 100% of height; horizontal scale = srcAspect/dstAspect.
        cbData.scaleX  = srcAspect / dstAspect;
        cbData.scaleY  = 1.0f;
        cbData.offsetX = (1.0f - cbData.scaleX) * 0.5f;  // center horizontally
        cbData.offsetY = 0.0f;
    }

    // ---- Debug letterbox params ----
    logger::debug("GPUScaler::Scale: letterbox src={}x{} dst={}x{} scale=({:.3f},{:.3f}) offset=({:.3f},{:.3f})",
                  srcWidth_, srcHeight_, dstWidth_, dstHeight_,
                  cbData.scaleX, cbData.scaleY, cbData.offsetX, cbData.offsetY);

    // ---- Upload cbuffer ----
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(cbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        logger::error("GPUScaler::Scale: cbuffer Map failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return nullptr;
    }
    std::memcpy(mapped.pData, &cbData, sizeof(cbData));
    context->Unmap(cbuffer_.Get(), 0);

    // ---- Render pass ----
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // Black letterbox bars
    context->ClearRenderTargetView(rtv_.Get(), clearColor);
    context->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);

    // Create SRV for the input (desktop) texture each frame
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inputSrv;
    {
        // Query actual texture format from input
        D3D11_TEXTURE2D_DESC inputDesc = {};
        input->GetDesc(&inputDesc);
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = inputDesc.Format;  // Use actual format, not hardcoded
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D = { 0, 1 };
        hr = device_->CreateShaderResourceView(input, &srvDesc, &inputSrv);
        if (FAILED(hr)) {
            logger::error("GPUScaler::Scale: CreateShaderResourceView(input) failed (hr=0x{:08x})", static_cast<unsigned>(hr));
            return nullptr;
        }
        logger::debug("GPUScaler::Scale: input format = {}", static_cast<int>(inputDesc.Format));
    }
    context->PSSetShaderResources(0, 1, inputSrv.GetAddressOf());
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->VSSetConstantBuffers(0, 1, cbuffer_.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, cbuffer_.GetAddressOf());

    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<FLOAT>(dstWidth_);
    vp.Height   = static_cast<FLOAT>(dstHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    context->RSSetViewports(1, &vp);

    context->IASetInputLayout(nullptr);  // SV_VertexID — no vertex buffer
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    context->Draw(4, 0);  // Fullscreen quad

    // Signal GPU completion via pre-created event query.
    // MFCreateDXGISurfaceBuffer reads the texture later; the query
    // ensures GPU work is queued without stalling the CPU.
    if (completionQuery_) {
        context->End(completionQuery_.Get());
    } else {
        // Fallback only if query creation failed in constructor
        context->Flush();
    }

    // ---- Unbind to avoid resource conflicts ----
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context->PSSetShaderResources(0, 1, &nullSrv);
    ID3D11RenderTargetView* nullRtv = nullptr;
    context->OMSetRenderTargets(1, &nullRtv, nullptr);

    return outputTexture_.Get();
}

// =============================================================================
// GPUScaler private
// =============================================================================

bool GPUScaler::CreateRenderTarget(ID3D11Device* device) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width       = dstWidth_;
    td.Height      = dstHeight_;
    td.MipLevels   = 1;
    td.ArraySize   = 1;
    td.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc  = { 1, 0 };
    td.Usage       = D3D11_USAGE_DEFAULT;
    td.BindFlags   = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.CPUAccessFlags = 0;
    td.MiscFlags   = 0;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, &outputTexture_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreateTexture2D (output) failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format        = td.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D     = { 0 };
    hr = device->CreateRenderTargetView(outputTexture_.Get(), &rtvDesc, &rtv_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreateRenderTargetView failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format        = td.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D     = { 0, 1 };
    return true;
}

bool GPUScaler::CreateShaders(ID3D11Device* device) {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    // ---- Vertex shader ----
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> vsErr;
    HRESULT hr = D3DCompile(kVertexShader, strlen(kVertexShader),
                            "vertex_shader", nullptr, nullptr,
                            "main", "vs_4_0", compileFlags, 0,
                            &vsBlob, &vsErr);
    if (FAILED(hr)) {
        if (vsErr) logger::error("GPUScaler VS compile error: {}",
                                static_cast<const char*>(vsErr->GetBufferPointer()));
        logger::error("GPUScaler: D3DCompile VS failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                    nullptr, &vs_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreateVertexShader failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    // ---- Pixel shader ----
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psErr;
    hr = D3DCompile(kPixelShader, strlen(kPixelShader),
                    "pixel_shader", nullptr, nullptr,
                    "main", "ps_4_0", compileFlags, 0,
                    &psBlob, &psErr);
    if (FAILED(hr)) {
        if (psErr) logger::error("GPUScaler PS compile error: {}",
                                static_cast<const char*>(psErr->GetBufferPointer()));
        logger::error("GPUScaler: D3DCompile PS failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                   nullptr, &ps_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreatePixelShader failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    // ---- Linear sampler (for smooth bilinear downscale) ----
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter         = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy  = 1;
    sd.MinLOD         = 0;
    sd.MaxLOD         = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreateSamplerState failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool GPUScaler::CreateConstantBuffer(ID3D11Device* device) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = sizeof(LetterboxCBuffer);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&bd, nullptr, &cbuffer_);
    if (FAILED(hr)) {
        logger::error("GPUScaler: CreateBuffer (cbuffer) failed (hr=0x{:08x})",
                      static_cast<unsigned>(hr));
        return false;
    }
    return true;
}