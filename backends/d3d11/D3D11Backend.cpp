// ArgonUI - backends/d3d11/D3D11Backend.cpp
#include "D3D11Backend.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "argonui/Core/Log.h"
#include "backends/common/ArgonHLSL.h"

namespace argon::ui {

namespace {

constexpr const char* kTag = "D3D11";

/// Slots reserved for context save/restore when drawing into a device we do not
/// own. A typical engine touches a few dozen; 2048 is a generous ceiling.
constexpr size_t kContextStateSlots = 2048;

const char* HResultName(HRESULT hr) {
    switch (hr) {
    case S_OK:                            return "S_OK";
    case E_FAIL:                          return "E_FAIL";
    case E_INVALIDARG:                    return "E_INVALIDARG";
    case E_OUTOFMEMORY:                   return "E_OUTOFMEMORY";
    case DXGI_ERROR_DEVICE_HUNG:          return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_REMOVED:       return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET:         return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    default:                              return "unknown HRESULT";
    }
}

DXGI_FORMAT ToDxgiFormat(TextureFormat f) {
    switch (f) {
    case TextureFormat::R8:    return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::BGRA8: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA8:
    default:                   return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

u32 BytesPerTexel(TextureFormat f) { return f == TextureFormat::R8 ? 1u : 4u; }

} // namespace

// ---------------------------------------------------------------------------

D3D11Backend::D3D11Backend() = default;
D3D11Backend::~D3D11Backend() { Shutdown(); }

void D3D11Backend::Fail(const char* what, long hr) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed: %s (0x%08lX)", what,
                  HResultName(static_cast<HRESULT>(hr)),
                  static_cast<unsigned long>(static_cast<HRESULT>(hr)));
    lastError_ = buf;
    ARGON_ERROR(kTag, "%s", buf);
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

bool D3D11Backend::Initialize(const DeviceDesc& desc) {
    info_.api = GraphicsApi::D3D11;
    info_.shadingLanguage = "HLSL";
    info_.ownsDevice = false;
    info_.ownsSwapchain = false;

    transparentBackground_ = desc.transparentBackground;
    windowHandle_ = desc.windowHandle;
    syncInterval_ = desc.enableVsync ? 1u : 0u;

    // When we render into somebody else's device we must not leave our state
    // behind; that is the default expectation for an overlay.
    preserveContextState_ = (desc.deviceSource == DeviceSource::External);

    if (desc.deviceSource == DeviceSource::External) {
        if (desc.externalDevice == nullptr || desc.externalContext == nullptr) {
            lastError_ = "External device source requires externalDevice and externalContext";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
        device_ = static_cast<ID3D11Device*>(desc.externalDevice);
        context_ = static_cast<ID3D11DeviceContext*>(desc.externalContext);
        ownsDevice_ = false;
        ARGON_INFO(kTag, "adopted external device %p", static_cast<void*>(device_));
    } else {
        static const D3D_FEATURE_LEVEL kLevels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3
        };
        D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
        UINT flags = desc.enableValidation ? D3D11_CREATE_DEVICE_DEBUG : 0u;

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels,
                                       ARRAYSIZE(kLevels), D3D11_SDK_VERSION, &device_, &obtained,
                                       &context_);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels,
                                   ARRAYSIZE(kLevels), D3D11_SDK_VERSION, &device_, &obtained,
                                   &context_);
            if (FAILED(hr)) { Fail("D3D11CreateDevice", hr); return false; }
            ARGON_WARN(kTag, "no hardware adapter; fell back to WARP");
        }
        ownsDevice_ = true;
        info_.ownsDevice = true;
    }

    if (device_ == nullptr || context_ == nullptr) {
        lastError_ = "device or context is null after creation";
        return false;
    }

    // Adapter name for Info(). Cheap, one-off.
    {
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice),
                                              reinterpret_cast<void**>(&dxgiDevice))) &&
            dxgiDevice != nullptr && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
            DXGI_ADAPTER_DESC ad{};
            if (SUCCEEDED(adapter->GetDesc(&ad))) {
                static thread_local char nameBuf[256];
                if (WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, nameBuf,
                                        static_cast<int>(sizeof(nameBuf)), nullptr, nullptr) > 0) {
                    info_.deviceName = nameBuf;
                }
                static thread_local char verBuf[128];
                std::snprintf(verBuf, sizeof(verBuf), "%u.%u.%u.%u",
                              HIWORD(ad.DriverVersion), LOWORD(ad.DriverVersion),
                              HIWORD(ad.DriverVersion), LOWORD(ad.DriverVersion));
                (void)verBuf;
            }
            if (adapter != nullptr) adapter->Release();
        }
        if (dxgiDevice != nullptr) dxgiDevice->Release();
    }

    if (preserveContextState_) {
        contextStateCache_.resize(kContextStateSlots);
    }

    if (!CompileShaders()) return false;
    if (!CreatePipeline()) return false;
    if (!CreateStaticResources()) return false;
    return true;
}

bool D3D11Backend::CompileShaders() {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(kArgonHlslSource, std::strlen(kArgonHlslSource), "argonui.hlsl", nullptr,
                            nullptr, "VSMain", "vs_4_0", flags, 0, &vsBlob_, &vsMessage_);
    if (FAILED(hr)) {
        Fail("D3DCompile(VSMain)", hr);
        if (vsMessage_ != nullptr) {
            ARGON_ERROR(kTag, "%s", vsMessage_->GetBufferPointer());
        }
        return false;
    }

    hr = D3DCompile(kArgonHlslSource, std::strlen(kArgonHlslSource), "argonui.hlsl", nullptr,
                    nullptr, "PSMain", "ps_4_0", flags, 0, &psBlob_, &psMessage_);
    if (FAILED(hr)) {
        Fail("D3DCompile(PSMain)", hr);
        if (psMessage_ != nullptr) {
            ARGON_ERROR(kTag, "%s", psMessage_->GetBufferPointer());
        }
        return false;
    }

    hr = device_->CreateVertexShader(vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize(),
                                     nullptr, &vs_);
    if (FAILED(hr)) { Fail("CreateVertexShader", hr); return false; }

    hr = device_->CreatePixelShader(psBlob_->GetBufferPointer(), psBlob_->GetBufferSize(), nullptr,
                                    &ps_);
    if (FAILED(hr)) { Fail("CreatePixelShader", hr); return false; }
    return true;
}

bool D3D11Backend::CreatePipeline() {
    HRESULT hr;

    // Input layout - mirrors argon::ui::Vertex (64 bytes) exactly.
    // Offsets come straight from the struct: hx@16, radii@24,
    // borderWidth/glowWidth@40, fill@48, border@52, glow@56, flags@60.
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,        0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        8,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       16,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 24,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT,       40,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32_UINT,           48,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 5, DXGI_FORMAT_R32_UINT,           52,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 6, DXGI_FORMAT_R32_UINT,           56,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 7, DXGI_FORMAT_R32_UINT,           60,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = device_->CreateInputLayout(elements, ARRAYSIZE(elements), vsBlob_->GetBufferPointer(),
                                    vsBlob_->GetBufferSize(), &inputLayout_);
    if (FAILED(hr)) { Fail("CreateInputLayout", hr); return false; }

    // Premultiplied alpha: the shader returns rgb * a, so the blend is
    // (ONE, ONE_MINUS_SRC_ALPHA). Matches the reference UIs exactly.
    D3D11_BLEND_DESC bd{};
    bd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    hr = device_->CreateBlendState(&bd, &blendAlpha_);
    if (FAILED(hr)) { Fail("CreateBlendState(alpha)", hr); return false; }

    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    hr = device_->CreateBlendState(&bd, &blendAdditive_);
    if (FAILED(hr)) { Fail("CreateBlendState(additive)", hr); return false; }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;   // the SDF is winding-independent
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;         // clipping is per-batch, done on the CPU
    hr = device_->CreateRasterizerState(&rd, &rasterizer_);
    if (FAILED(hr)) { Fail("CreateRasterizerState", hr); return false; }

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = device_->CreateDepthStencilState(&ds, &depthState_);
    if (FAILED(hr)) { Fail("CreateDepthStencilState", hr); return false; }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) { Fail("CreateSamplerState", hr); return false; }

    return true;
}

bool D3D11Backend::CreateStaticResources() {
    const u32 white = 0xFFFFFFFFu;
    TextureRecord rec;
    rec.desc.width = 1;
    rec.desc.height = 1;
    rec.desc.format = TextureFormat::RGBA8;
    if (!CreateTextureInternal(rec.desc, &white, rec)) return false;
    whiteSrv_ = rec.srv;
    textures_.emplace(1u, rec);
    return true;
}

// ---------------------------------------------------------------------------
// Target
// ---------------------------------------------------------------------------

bool D3D11Backend::CreateSwapchainForWindow(const TargetDesc& desc, u32 width, u32 height) {
    IDXGIFactory2* factory = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, &factory);
    if (FAILED(hr) || factory == nullptr) { Fail("CreateDXGIFactory2", hr); return false; }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = desc.msaaSamples > 0 ? desc.msaaSamples : 1u;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    // DISCARD keeps compatibility with the widest range of host windows and is
    // what most engines create, which matters when adopting a foreign chain.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.AlphaMode = transparentBackground_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1* chain = nullptr;
    hr = factory->CreateSwapChainForHwnd(context_, &sd, nullptr, nullptr, &chain);
    factory->Release();
    if (FAILED(hr) || chain == nullptr) { Fail("CreateSwapChainForHwnd", hr); return false; }

    // Keep Alt+Enter from toggling fullscreen behind the host's back.
    factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory2(0, &factory)) && factory != nullptr) {
        factory->MakeWindowAssociation(reinterpret_cast<HWND>(windowHandle_), 2 /* DXGI_MWA_NO_ALT_ENTER */);
        factory->Release();
    }

    swapChain_ = chain;
    ownsSwapchain_ = true;
    info_.ownsSwapchain = true;
    return BuildRtvFromSwapchain();
}

bool D3D11Backend::AdoptTarget(const TargetDesc& desc) {
    if (desc.externalSwapchain == nullptr) {
        lastError_ = "AdoptTarget was given a null swapchain";
        return false;
    }
    if (swapChain_ != nullptr) {
        lastError_ = "a swapchain is already attached; call DestroyTarget() first";
        return false;
    }
    swapChain_ = static_cast<IDXGISwapChain*>(desc.externalSwapchain);
    ownsSwapchain_ = false;
    info_.ownsSwapchain = false;
    ARGON_INFO(kTag, "adopted external swapchain %p", desc.externalSwapchain);
    return BuildRtvFromSwapchain();
}

bool D3D11Backend::CreateTarget(const TargetDesc& desc) {
    DestroyTarget();
    if (device_ == nullptr || context_ == nullptr) {
        lastError_ = "CreateTarget called before Initialize()";
        return false;
    }
    if (desc.externalSwapchain != nullptr) return AdoptTarget(desc);

    HWND hwnd = static_cast<HWND>(windowHandle_);
    if (hwnd == nullptr) {
        lastError_ = "CreateTarget needs DeviceDesc::windowHandle to create a swapchain";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    const u32 w = desc.width > 0 ? desc.width : 1u;
    const u32 h = desc.height > 0 ? desc.height : 1u;
    return CreateSwapchainForWindow(desc, w, h);
}

bool D3D11Backend::BuildRtvFromSwapchain() {
    if (swapChain_ == nullptr || device_ == nullptr) return false;

    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_));
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetBuffer", hr); return false; }

    DXGI_SWAP_CHAIN_DESC sd{};
    hr = swapChain_->GetDesc(&sd);
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetDesc", hr); return false; }

    hr = device_->CreateRenderTargetView(backBuffer_, nullptr, &rtv_);
    if (FAILED(hr)) { Fail("CreateRenderTargetView", hr); return false; }

    targetWidth_ = sd.BufferDesc.Width;
    targetHeight_ = sd.BufferDesc.Height;
    targetSampleCount_ = sd.SampleDesc.Count;
    targetSize_ = Vec2(static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
    return true;
}

void D3D11Backend::DestroyTarget() {
    if (rtv_ != nullptr) { rtv_->Release(); rtv_ = nullptr; }
    if (backBuffer_ != nullptr) { backBuffer_->Release(); backBuffer_ = nullptr; }
    if (swapChain_ != nullptr) {
        // An adopted chain is not ours to release.
        if (ownsSwapchain_) swapChain_->Release();
        swapChain_ = nullptr;
    }
    ownsSwapchain_ = false;
    info_.ownsSwapchain = false;
    targetWidth_ = targetHeight_ = 0;
    targetSampleCount_ = 1;
    targetSize_ = Vec2(0.0f, 0.0f);
}

bool D3D11Backend::ResizeTarget(u32 width, u32 height) {
    if (swapChain_ == nullptr) return false;
    if (width == 0 || height == 0) return true;              // minimised
    if (width == targetWidth_ && height == targetHeight_) return true;

    // Every back-buffer reference must be gone or ResizeBuffers returns
    // DXGI_ERROR_INVALIDCALL, which is the usual cause of a black overlay.
    if (rtv_ != nullptr) { rtv_->Release(); rtv_ = nullptr; }
    if (backBuffer_ != nullptr) { backBuffer_->Release(); backBuffer_ = nullptr; }

    const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        Fail("IDXGISwapChain::ResizeBuffers", hr);
        if (hr == DXGI_ERROR_INVALIDCALL) {
            lastError_ += " - a back-buffer reference is still held somewhere";
        }
        return false;
    }
    return BuildRtvFromSwapchain();
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

bool D3D11Backend::CreateTextureInternal(const TextureDesc& desc, const void* pixels,
                                         TextureRecord& out) {
    out.desc = desc;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = ToDxgiFormat(desc.format);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &out.texture);
    if (FAILED(hr)) { Fail("CreateTexture2D", hr); return false; }

    hr = device_->CreateShaderResourceView(out.texture, nullptr, &out.srv);
    if (FAILED(hr)) { Fail("CreateShaderResourceView", hr); out.texture->Release(); out.texture = nullptr; return false; }

    // A staging copy is needed before UpdateSubresource is legal on a DEFAULT texture.
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    hr = device_->CreateTexture2D(&td, nullptr, &out.staging);
    if (FAILED(hr)) { Fail("CreateTexture2D(staging)", hr); return false; }

    if (pixels != nullptr) {
        if (!UploadTextureData(desc, pixels, out)) return false;
    }
    return true;
}

bool D3D11Backend::UploadTextureData(const TextureDesc& desc, const void* pixels,
                                     TextureRecord& rec) {
    const u32 bpp = BytesPerTexel(desc.format);
    const size_t rowBytes = static_cast<size_t>(desc.width) * bpp;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context_->Map(rec.staging, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) { Fail("Map(staging)", hr); return false; }

    if (static_cast<size_t>(mapped.RowPitch) == rowBytes) {
        std::memcpy(mapped.pData, pixels, rowBytes * desc.height);
    } else {
        // Row pitch is padded for alignment; copy row by row.
        const u8* src = static_cast<const u8*>(pixels);
        for (u32 y = 0; y < desc.height; ++y) {
            std::memcpy(static_cast<u8*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                        src + static_cast<size_t>(y) * rowBytes, rowBytes);
        }
    }
    context_->Unmap(rec.staging, 0);

    context_->CopyResource(rec.texture, rec.staging);
    return true;
}

u32 D3D11Backend::CreateTexture(const TextureDesc& desc, const void* pixels) {
    if (device_ == nullptr || desc.width == 0 || desc.height == 0) return 0;

    TextureRecord rec;
    if (!CreateTextureInternal(desc, pixels, rec)) return 0;

    const u32 id = nextTextureId_++;
    textures_.emplace(id, rec);
    return id;
}

bool D3D11Backend::UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) {
    auto it = textures_.find(handle);
    if (it == textures_.end() || pixels == nullptr) return false;

    if (desc.width != it->second.desc.width || desc.height != it->second.desc.height ||
        desc.format != it->second.desc.format) {
        // A format or size change needs a full re-create.
        TextureRecord fresh;
        if (!CreateTextureInternal(desc, nullptr, fresh)) return false;
        if (!UploadTextureData(desc, pixels, fresh)) return false;

        TextureRecord& old = it->second;
        if (old.srv != nullptr) old.srv->Release();
        if (old.staging != nullptr) old.staging->Release();
        if (old.texture != nullptr) old.texture->Release();
        old = fresh;
        return true;
    }
    return UploadTextureData(desc, pixels, it->second);
}

void D3D11Backend::DestroyTexture(u32 handle) {
    if (handle == 1 || handle == 0) return;  // the white 1x1 is not ours to free here
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;
    if (it->second.srv != nullptr) it->second.srv->Release();
    if (it->second.staging != nullptr) it->second.staging->Release();
    if (it->second.texture != nullptr) it->second.texture->Release();
    textures_.erase(it);
}

ID3D11ShaderResourceView* D3D11Backend::ResolveSrv(u32 handle) const {
    if (handle == 0 || handle == 1) return whiteSrv_;
    auto it = textures_.find(handle);
    return it == textures_.end() ? whiteSrv_ : it->second.srv;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

bool D3D11Backend::EnsureVertexCapacity(size_t vertices, size_t indices) {
    const size_t vBytes = vertices * sizeof(Vertex);
    const size_t iBytes = indices * sizeof(u32);

    if (vertexBuffer_ == nullptr || vBytes > vertexCapacity_) {
        if (vertexBuffer_ != nullptr) { vertexBuffer_->Release(); vertexBuffer_ = nullptr; }
        vertexCapacity_ = Max<size_t>(vBytes * 2, 64 * 1024);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(vertexCapacity_);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device_->CreateBuffer(&bd, nullptr, &vertexBuffer_);
        if (FAILED(hr)) { Fail("CreateBuffer(vertex)", hr); vertexCapacity_ = 0; return false; }
    }

    if (indexBuffer_ == nullptr || iBytes > indexCapacity_) {
        if (indexBuffer_ != nullptr) { indexBuffer_->Release(); indexBuffer_ = nullptr; }
        indexCapacity_ = Max<size_t>(iBytes * 2, 32 * 1024);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(indexCapacity_);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device_->CreateBuffer(&bd, nullptr, &indexBuffer_);
        if (FAILED(hr)) { Fail("CreateBuffer(index)", hr); indexCapacity_ = 0; return false; }
    }
    return true;
}

bool D3D11Backend::BeginFrame(const FrameState& state) {
    if (frameBegun_) return false;                 // unbalanced Begin/End
    if (context_ == nullptr) return false;
    if (rtv_ == nullptr) return false;             // no target: nothing to draw into
    if (state.occluded) return false;              // minimised / hidden

    frameBegun_ = true;
    drawCallCount_ = 0;

    if (preserveContextState_ && !contextStateCache_.empty()) {
        UINT count = static_cast<UINT>(contextStateCache_.size());
        context_->GetState(contextStateCache_.data(),
                           static_cast<UINT>(contextStateCache_.size() * sizeof(D3D11_CONTEXT_STATE)),
                           &count);
    }
    return true;
}

void D3D11Backend::Render(const DrawList& list, const FrameState&) {
    if (!frameBegun_ || context_ == nullptr || rtv_ == nullptr) return;
    if (list.CmdCount() == 0) return;

    const size_t vertexBytes = list.VertexCount() * sizeof(Vertex);
    const size_t indexBytes = list.IndexCount() * sizeof(u32);
    if (!EnsureVertexCapacity(list.VertexCount(), list.IndexCount())) return;

    // Upload.
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { Fail("Map(vertexBuffer)", hr); return; }
        std::memcpy(mapped.pData, list.Vertices(), vertexBytes);
        context_->Unmap(vertexBuffer_, 0);

        hr = context_->Map(indexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { Fail("Map(indexBuffer)", hr); return; }
        std::memcpy(mapped.pData, list.Indices(), indexBytes);
        context_->Unmap(indexBuffer_, 0);
    }

    // State.
    ID3D11RenderTargetView* rtv = rtv_;
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->RSSetState(rasterizer_);
    context_->OMSetDepthStencilState(depthState_, 0);
    context_->GSSetState(nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<f32>(targetWidth_);
    vp.Height = static_cast<f32>(targetHeight_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    context_->RSSetViewports(1, &vp);

    // The shader turns pixel coordinates into clip space via this constant.
    {
        const float viewportConst[4] = {
            static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_),
            1.0f / static_cast<f32>(Max(1u, targetWidth_)),
            1.0f / static_cast<f32>(Max(1u, targetHeight_))
        };
        if (viewportCB_ == nullptr) {
            D3D11_BUFFER_DESC cbd{};
            cbd.ByteWidth = 16;
            cbd.Usage = D3D11_USAGE_DYNAMIC;
            cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&cbd, nullptr, &viewportCB_);
            if (FAILED(hr)) { Fail("CreateBuffer(viewport cbuffer)", hr); return; }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(viewportCB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { Fail("Map(viewport cbuffer)", hr); return; }
        std::memcpy(mapped.pData, viewportConst, sizeof(viewportConst));
        context_->Unmap(viewportCB_, 0);

        context_->VSSetConstantBuffers(0, 1, &viewportCB_);
        context_->PSSetConstantBuffers(0, 1, &viewportCB_);
    }

    context_->VSSetShader(vs_, nullptr, 0);
    context_->PSSetShader(ps_, nullptr, 0);
    context_->IASetInputLayout(inputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    context_->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R32_UINT, 0);

    context_->PSSetSamplers(0, 1, &sampler_);

    // Draw every batch, switching only what actually differs between them.
    ID3D11BlendState* boundBlend = nullptr;
    ID3D11ShaderResourceView* boundSrv = nullptr;
    RECT boundScissor{};

    const DrawCmd* cmds = list.Cmds();
    const u32 cmdCount = list.CmdCount();

    for (u32 i = 0; i < cmdCount; ++i) {
        const DrawCmd& cmd = cmds[i];
        if (cmd.indexCount == 0) continue;

        // Scissor: clamp the CPU-side clip to the target, floor/ceil so partial
        // pixel coverage is never cut off.
        RECT scissor{};
        const f32 l = Max(0.0f, floor(cmd.clip.min.x));
        const f32 t = Max(0.0f, floor(cmd.clip.min.y));
        const f32 r = Min(static_cast<f32>(targetWidth_), ceil(cmd.clip.max.x));
        const f32 b = Min(static_cast<f32>(targetHeight_), ceil(cmd.clip.max.y));
        scissor.left = static_cast<LONG>(l);
        scissor.top = static_cast<LONG>(t);
        scissor.right = static_cast<LONG>(Max(l + 1.0f, r));
        scissor.bottom = static_cast<LONG>(Max(t + 1.0f, b));

        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;

        ID3D11BlendState* want = (cmd.blend == BlendMode::Additive) ? blendAdditive_ : blendAlpha_;
        if (want != boundBlend) {
            context_->OMSetBlendState(want, nullptr, 0xFFFFFFFFu);
            boundBlend = want;
        }

        ID3D11ShaderResourceView* wantSrv = ResolveSrv(cmd.textureId);
        if (wantSrv != boundSrv) {
            context_->PSSetShaderResources(0, 1, &wantSrv);
            boundSrv = wantSrv;
        }

        if (boundScissor.left != scissor.left || boundScissor.top != scissor.top ||
            boundScissor.right != scissor.right || boundScissor.bottom != scissor.bottom) {
            context_->RSSetScissorRects(1, &scissor);
            boundScissor = scissor;
        }

        context_->DrawIndexed(cmd.indexCount, static_cast<INT>(cmd.indexOffset), 0);
        ++drawCallCount_;
    }
}

bool D3D11Backend::Present() {
    if (!ownsSwapchain_ || swapChain_ == nullptr) return false;
    if (!frameBegun_) return false;
    // Skip presenting when the window is hidden; DXGI would error.
    const HRESULT hr = swapChain_->Present(syncInterval_, 0);
    return SUCCEEDED(hr);
}

void D3D11Backend::EndFrame() {
    if (!frameBegun_) return;

    if (preserveContextState_ && !contextStateCache_.empty()) {
        UINT count = static_cast<UINT>(contextStateCache_.size());
        context_->SetState(contextStateCache_.data(),
                           static_cast<UINT>(contextStateCache_.size() * sizeof(D3D11_CONTEXT_STATE)),
                           &count);
    } else {
        // Unbind so an adopted swapchain is left in a predictable state.
        static ID3D11ShaderResourceView* const nullSrv[1] = {nullptr};
        static ID3D11SamplerState* const nullSampler[1] = {nullptr};
        context_->PSSetShaderResources(0, 1, nullSrv);
        context_->PSSetSamplers(0, 1, nullSampler);
    }
    frameBegun_ = false;
}

void D3D11Backend::Shutdown() {
    DestroyTarget();

    for (auto& kv : textures_) {
        if (kv.second.srv != nullptr) kv.second.srv->Release();
        if (kv.second.staging != nullptr) kv.second.staging->Release();
        if (kv.second.texture != nullptr) kv.second.texture->Release();
    }
    textures_.clear();
    whiteSrv_ = nullptr;
    nextTextureId_ = 2;

    if (vertexBuffer_ != nullptr) { vertexBuffer_->Release(); vertexBuffer_ = nullptr; }
    if (indexBuffer_ != nullptr) { indexBuffer_->Release(); indexBuffer_ = nullptr; }
    if (viewportCB_ != nullptr) { viewportCB_->Release(); viewportCB_ = nullptr; }
    vertexCapacity_ = indexCapacity_ = 0;

    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (depthState_ != nullptr) { depthState_->Release(); depthState_ = nullptr; }
    if (rasterizer_ != nullptr) { rasterizer_->Release(); rasterizer_ = nullptr; }
    if (blendAdditive_ != nullptr) { blendAdditive_->Release(); blendAdditive_ = nullptr; }
    if (blendAlpha_ != nullptr) { blendAlpha_->Release(); blendAlpha_ = nullptr; }
    if (inputLayout_ != nullptr) { inputLayout_->Release(); inputLayout_ = nullptr; }
    if (ps_ != nullptr) { ps_->Release(); ps_ = nullptr; }
    if (vs_ != nullptr) { vs_->Release(); vs_ = nullptr; }
    if (psBlob_ != nullptr) { psBlob_->Release(); psBlob_ = nullptr; }
    if (vsBlob_ != nullptr) { vsBlob_->Release(); vsBlob_ = nullptr; }
    if (psMessage_ != nullptr) { psMessage_->Release(); psMessage_ = nullptr; }
    if (vsMessage_ != nullptr) { vsMessage_->Release(); vsMessage_ = nullptr; }

    if (ownsDevice_ && context_ != nullptr) context_->Flush();
    if (ownsDevice_ && context_ != nullptr) context_->Release();
    if (ownsDevice_ && device_ != nullptr) device_->Release();

    context_ = nullptr;
    device_ = nullptr;
    ownsDevice_ = false;
    contextStateCache_.clear();
    frameBegun_ = false;
}

// ---------------------------------------------------------------------------

IArgonBackend* CreateD3D11Backend() { return new D3D11Backend(); }

} // namespace argon::ui