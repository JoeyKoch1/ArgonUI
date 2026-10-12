// ArgonUI - backends/d3d11/D3D11Backend.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "argonui/Backend/Backend.h"

namespace argon::ui {
class D3D11Backend final : public IArgonBackend {
public:
    D3D11Backend();
    ~D3D11Backend() override;

    // -- IArgonBackend -----------------------------------------------------
    bool Initialize(const DeviceDesc& desc) override;
    void Shutdown() override;

    bool CreateTarget(const TargetDesc& desc) override;
    bool AdoptTarget(const TargetDesc& desc) override;
    bool ResizeTarget(u32 width, u32 height) override;
    void DestroyTarget() override;
    bool HasTarget() const override { return rtv_ != nullptr; }
    Vec2 TargetSize() const override { return targetSize_; }

    bool BeginFrame(const FrameState& state) override;
    void Render(const DrawList& list, const FrameState& state) override;
    bool Present() override;
    void EndFrame() override;

    u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) override;
    bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) override;
    void DestroyTexture(u32 handle) override;

    const BackendInfo& Info() const override { return info_; }
    const char* Name() const override { return "D3D11"; }
    const char* LastError() const override { return lastError_.c_str(); }

    // -- Escape hatches for hosts that interleave their own draws ---------
    ID3D11Device* Device() const { return device_; }
    ID3D11DeviceContext* Context() const { return context_; }
    IDXGISwapChain* SwapChain() const { return swapChain_; }
    ID3D11RenderTargetView* RenderTarget() const { return rtv_; }
    u32 DrawCallCount() const { return drawCallCount_; }

private:
    struct TextureRecord {
        ID3D11Texture2D* texture = nullptr;
        ID3D11Texture2D* staging = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        TextureDesc desc{};
    };

    bool CompileShaders();
    bool CreatePipeline();
    bool CreateStaticResources();
    bool CreateSwapchainForWindow(const TargetDesc& desc, u32 width, u32 height);
    bool BuildRtvFromSwapchain();
    bool EnsureVertexCapacity(size_t vertices, size_t indices);
    bool CreateTextureInternal(const TextureDesc& desc, const void* pixels, TextureRecord& out);
    bool UploadTextureData(const TextureDesc& desc, const void* pixels, TextureRecord& rec);
    ID3D11ShaderResourceView* ResolveSrv(u32 handle) const;
    void Fail(const char* what, long hr);

    // -- Device / target ---------------------------------------------------
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11Texture2D* backBuffer_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;

    bool ownsDevice_ = false;
    bool ownsSwapchain_ = false;
    bool preserveContextState_ = false;
    bool transparentBackground_ = false;
    void* windowHandle_ = nullptr;
    UINT syncInterval_ = 1;

    u32 targetWidth_ = 0;
    u32 targetHeight_ = 0;
    u32 targetSampleCount_ = 1;
    Vec2 targetSize_{0.0f, 0.0f};

    // -- Pipeline ----------------------------------------------------------
    ID3DBlob* vsBlob_ = nullptr;
    ID3DBlob* psBlob_ = nullptr;
    ID3DBlob* vsMessage_ = nullptr;
    ID3DBlob* psMessage_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11BlendState* blendAlpha_ = nullptr;
    ID3D11BlendState* blendAdditive_ = nullptr;
    ID3D11RasterizerState* rasterizer_ = nullptr;
    ID3D11DepthStencilState* depthState_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11ShaderResourceView* whiteSrv_ = nullptr;

    ID3D11Buffer* vertexBuffer_ = nullptr;
    ID3D11Buffer* indexBuffer_ = nullptr;
    ID3D11Buffer* viewportCB_ = nullptr;
    size_t vertexCapacity_ = 0;
    size_t indexCapacity_ = 0;

    // -- Textures ----------------------------------------------------------
    std::unordered_map<u32, TextureRecord> textures_;
    u32 nextTextureId_ = 2;  // id 1 is the built-in white 1x1

    // -- Frame -------------------------------------------------------------
    bool frameBegun_ = false;
    u32 drawCallCount_ = 0;
    std::vector<D3D11_CONTEXT_STATE> contextStateCache_;

    BackendInfo info_{};
    std::string lastError_;
};

} // namespace argon::ui
