// ArgonUI - backends/d3d12/D3D12Backend.cpp
#include "D3D12Backend.h"

#include <d3d12sdklayers.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "argonui/Core/Log.h"
#include "backends/common/ArgonHLSL.h"

namespace argon::ui {
namespace {
constexpr const char* kTag = "D3D12";

/// The shader in backends/common/ArgonHLSL.h declares `Texture2D gTexture;` and
/// `SamplerState gSampler;` with no explicit register. In D3D12 that means
/// register space 0, slot 0 - exactly where the root signature below puts them,
/// so this prefix only carries documentation and an API marker.
constexpr const char* kArgonD3D12Prefix =
    "// ArgonUI D3D12 backend prefix\n"
    "#define ARGON_D3D12 1\n";

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

UINT64 AlignUp(UINT64 value, UINT64 alignment) {
    if (alignment == 0) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

/// Buffer resources only need 4-byte alignment, but 256 keeps every stride a
/// round number for the debug layer and costs nothing. (D3D12_DEFAULT_RESOURCE_ALIGNMENT
/// is 64 KiB and applies to texture subresource sizes, not to buffer widths.)
constexpr UINT64 kBufferAlignment = 256;

D3D12_RESOURCE_BARRIER MakeTransition(ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATE before,
                                      D3D12_RESOURCE_STATE after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

HRESULT CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count,
                             bool shaderVisible, ComPtr<ID3D12DescriptorHeap>* out) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                               : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    return device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), out->GetAddressOf());
}

void EnableDebugLayer() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.GetAddressOf())) &&
        debug != nullptr) {
        debug->EnableDebugLayer();
    } else {
        ARGON_WARN(kTag, "debug layer unavailable; continuing without validation");
    }
}

} // namespace

D3D12Backend::D3D12Backend() = default;
D3D12Backend::~D3D12Backend() { Shutdown(); }

void D3D12Backend::Fail(const char* what, long hr) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed: %s (0x%08lX)", what,
                  HResultName(static_cast<HRESULT>(hr)),
                  static_cast<unsigned long>(static_cast<HRESULT>(hr)));
    lastError_ = buf;
    ARGON_ERROR(kTag, "%s", buf);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::RenderTargetHandle() const {
    if (backBufferRtv_.empty()) return D3D12_CPU_DESCRIPTOR_HANDLE{};
    return backBufferRtv_[0];
}

// Device

bool D3D12Backend::Initialize(const DeviceDesc& desc) {
    info_.api = GraphicsApi::D3D12;
    info_.shadingLanguage = "HLSL";
    info_.ownsDevice = false;
    info_.ownsSwapchain = false;

    transparentBackground_ = desc.transparentBackground;
    windowHandle_ = desc.windowHandle;
    syncInterval_ = desc.enableVsync ? 1u : 0u;
    msaaSamples_ = desc.msaaSamples > 0 ? desc.msaaSamples : 1u;

    if (desc.deviceSource == DeviceSource::External) {
        if (desc.externalDevice == nullptr || desc.externalContext == nullptr) {
            lastError_ =
                "External device source requires externalDevice (ID3D12Device*) and "
                "externalContext (ID3D12CommandQueue*)";
            ARGON_ERROR(kTag, "%s", lastError_.c_str());
            return false;
        }
        // Borrowed, never destroyed. Shutdown() Detaches rather than Resets.
        device_ = static_cast<ID3D12Device*>(desc.externalDevice);
        queue_ = static_cast<ID3D12CommandQueue*>(desc.externalContext);
        ownsDevice_ = false;
        ownsQueue_ = false;
        ARGON_INFO(kTag, "adopted external device %p queue %p", static_cast<void*>(device_.Get()),
                   static_cast<void*>(queue_.Get()));
    } else {
        // D3D12 has no "create device with debug" flag; the debug layer is a
        // separate interface toggled before the device exists.
        if (desc.enableValidation) EnableDebugLayer();

        HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                       device_.GetAddressOf());
        if (FAILED(hr) || device_ == nullptr) {
            // Headless VM, RDP session, blocklisted adapter: fall back to WARP
            // rather than failing outright - the same courtesy the D3D11
            // backend extends.
            ComPtr<IDXGIFactory4> factory;
            ComPtr<IDXGIAdapter1> warpAdapter;
            HRESULT warpHr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory4), factory.GetAddressOf());
            if (FAILED(warpHr) || factory == nullptr) { Fail("CreateDXGIFactory2", warpHr); return false; }

            warpHr = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), warpAdapter.GetAddressOf());
            if (FAILED(warpHr) || warpAdapter == nullptr) { Fail("IDXGIFactory::EnumWarpAdapter", warpHr); return false; }

            warpHr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                       device_.GetAddressOf());
            if (FAILED(warpHr) || device_ == nullptr) { Fail("D3D12CreateDevice(WARP)", warpHr); return false; }
            ARGON_WARN(kTag, "no hardware adapter; fell back to WARP");
        }

        // A D3D12 device renders nothing on its own: without a queue there is
        // no command list to submit and no fence to signal.
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0;

        HRESULT hr = device_->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                                  queue_.GetAddressOf());
        if (FAILED(hr) || queue_ == nullptr) { Fail("CreateCommandQueue", hr); return false; }

        ownsDevice_ = true;
        ownsQueue_ = true;
        info_.ownsDevice = true;
    }

    if (device_ == nullptr || queue_ == nullptr) {
        lastError_ = "device or command queue is null after creation";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    // Adapter name for Info(). Cheap, one-off.
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter1> adapter;
        if (SUCCEEDED(device_.As(&dxgiDevice)) && dxgiDevice != nullptr &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
            DXGI_ADAPTER_DESC1 ad{};
            if (SUCCEEDED(adapter->GetDesc1(&ad))) {
                char nameBuf[256]{};
                if (WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, nameBuf,
                                        static_cast<int>(sizeof(nameBuf)), nullptr, nullptr) > 0) {
                    deviceName_ = nameBuf;
                }
                char verBuf[64]{};
                std::snprintf(verBuf, sizeof(verBuf), "%u.%u.%u.%u", HIWORD(ad.DriverVersion),
                              LOWORD(ad.DriverVersion), HIWORD(ad.DedicatedVideoMemory),
                              LOWORD(ad.DedicatedVideoMemory));
                driverVersion_ = verBuf;
            }
        }
        // These std::strings must outlive the const char* we hand out below.
        info_.deviceName = deviceName_.c_str();
        info_.driverVersion = driverVersion_.c_str();
    }

    if (!CompileShaders()) return false;
    if (!CreateRootSignature()) return false;
    if (!CreateStaticDescriptorHeaps()) return false;
    if (!CreateViewportConstantBuffer()) return false;
    if (!CreateFrameRing()) return false;
    if (!CreateGeometryBuffers()) return false;
    if (!CreateStaticResources()) return false;
    return true;
}

void D3D12Backend::Shutdown() {
    // The D3D12 equivalent of D3D11's context->Flush(): nothing may be torn
    // down while the GPU still has a reference to it.
    if (queue_ != nullptr) WaitForPendingFrames();

    DestroyTarget();

    // Textures own their DEFAULT resource and their upload staging buffer.
    textures_.clear();

    copyList_.Reset();
    copyAllocator_.Reset();

    frameSlots_.clear();

    viewportMapped_ = nullptr;
    viewportBuffer_.Reset();
    viewportGpuAddress_ = 0;

    rootDescriptorHeap_.Reset();
    srvHeap_.Reset();

    vertexMapped_ = nullptr;
    vertexBuffer_.Reset();
    vertexUpload_.Reset();
    indexMapped_ = nullptr;
    indexBuffer_.Reset();
    indexUpload_.Reset();
    vertexCapacity_ = 0;
    indexCapacity_ = 0;
    geometryDirty_ = true;

    pipelineAlpha_.Reset();
    pipelineAdditive_.Reset();
    rootSignature_.Reset();

    vsBlob_.Reset();
    psBlob_.Reset();
    vsMessage_.Reset();
    psMessage_.Reset();

    // A ComPtr releases on destruction, and the host's device and queue are not
    // ours to destroy. Detach() drops our reference *without* calling Release().
    if (!ownsQueue_) queue_.Detach();
    if (!ownsDevice_) device_.Detach();
    queue_.Reset();
    device_.Reset();
    ownsDevice_ = false;
    ownsQueue_ = false;

    deviceName_.clear();
    driverVersion_.clear();
    info_ = BackendInfo{};
    info_.api = GraphicsApi::D3D12;
    info_.shadingLanguage = "HLSL";

    frameSlot_ = 0;
    frameCounter_ = 0;
    frameBegun_ = false;
    workSubmitted_ = false;
    drawCallCount_ = 0;
    nextTextureId_ = 2;
    maxTextureSize_ = 0;
    vertexBytesUploaded_ = 0;
    indexBytesUploaded_ = 0;
    indexCountUploaded_ = 0;
    lastError_.clear();
}

// Shaders / root signature / heaps

bool D3D12Backend::CompileShaders() {
    const std::string source = std::string(kArgonD3D12Prefix) + kArgonHlslSource;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(source.c_str(), static_cast<UINT>(source.size()), "argonui.hlsl", nullptr,
                            nullptr, "VSMain", "vs_5_0", flags, 0, vsBlob_.GetAddressOf(),
                            vsMessage_.GetAddressOf());
    if (FAILED(hr)) {
        Fail("D3DCompile(VSMain)", hr);
        if (vsMessage_ != nullptr) ARGON_ERROR(kTag, "%s", vsMessage_->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(source.c_str(), static_cast<UINT>(source.size()), "argonui.hlsl", nullptr, nullptr,
                    "PSMain", "ps_5_0", flags, 0, psBlob_.GetAddressOf(), psMessage_.GetAddressOf());
    if (FAILED(hr)) {
        Fail("D3DCompile(PSMain)", hr);
        if (psMessage_ != nullptr) ARGON_ERROR(kTag, "%s", psMessage_->GetBufferPointer());
        return false;
    }
    return true;
}

bool D3D12Backend::CreateRootSignature() {
    // b0: gViewport. Constant buffer views live in a CBV heap; the range below
    // points at one descriptor and the frame sets its GPU address with
    // SetGraphicsRootConstantBufferView.
    D3D12_DESCRIPTOR_RANGE cbvRange{};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    cbvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAGS_NONE;

    // t0: the bound texture. One SRV, pixel stage only - the vertex shader
    // never samples.
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAGS_NONE;

    // The shader's `SamplerState gSampler;` has no register either, so it needs
    // a static sampler at s0. There is no D3D12 equivalent of D3D11's
    // PSSetSamplers: a sampler is either baked into the root signature or it
    // is not bound. ArgonUI's sampler state is constant (linear, clamp), so
    // that is not a limitation in practice - but it does mean
    // TextureDesc::filterLinear / ::repeat are not honoured, exactly as in the
    // D3D11 backend where a single sampler state is created and shared.
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 0;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.pDescriptorRanges = &cbvRange;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX | D3D12_SHADER_VISIBILITY_PIXEL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_STATIC_SAMPLER;
    params[2].StaticSampler = sampler;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = ARRAYSIZE(params);
    rs.pParameters = params;
    // Required whenever an input layout is used with a root signature.
    rs.Flags = D3D_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (FAILED(hr)) {
        Fail("D3D12SerializeRootSignature", hr);
        if (errors != nullptr) {
            ARGON_ERROR(kTag, "%s", static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        return false;
    }

    hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                      __uuidof(ID3D12RootSignature), rootSignature_.GetAddressOf());
    blob->Release();
    if (errors != nullptr) errors->Release();
    if (FAILED(hr)) { Fail("CreateRootSignature", hr); return false; }
    return true;
}

bool D3D12Backend::CreateStaticDescriptorHeaps() {
    // Slot 0 is the built-in white 1x1, slots 1.. are host textures.
    HRESULT hr = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV, 1, true,
                                      &rootDescriptorHeap_);
    if (FAILED(hr)) { Fail("CreateDescriptorHeap(CBV)", hr); return false; }

    hr = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SRV, kMaxSrvDescriptors, true,
                              &srvHeap_);
    if (FAILED(hr)) { Fail("CreateDescriptorHeap(SRV)", hr); return false; }

    srvHeapStart_ = srvHeap_->GetDescriptorHandleForHeapStart();
    srvIncrement_ = srvHeap_->GetDescriptorIncrementSize();
    return true;
}

bool D3D12Backend::CreateViewportConstantBuffer() {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kViewportBufferBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  __uuidof(ID3D12Resource),
                                                  viewportBuffer_.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateCommittedResource(viewport)", hr); return false; }

    // Mapped once and never unmapped. Safe because BeginFrame() drains the GPU
    // before every write (see the note at the top of the header).
    D3D12_RANGE readRange{0, 0};
    hr = viewportBuffer_->Map(0, &readRange, &viewportMapped_);
    if (FAILED(hr)) { Fail("Map(viewport)", hr); return false; }
    std::memset(viewportMapped_, 0, kViewportBufferBytes);

    D3D12_CPU_DESCRIPTOR_HANDLE cbv = rootDescriptorHeap_->GetDescriptorHandleForHeapStart();
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
    cbvDesc.BufferLocation = viewportBuffer_->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = kViewportBufferBytes;
    device_->CreateConstantBufferView(&cbvDesc, cbv);
    viewportGpuAddress_ = viewportBuffer_->GetGPUVirtualAddress();
    return true;
}

bool D3D12Backend::CreateFrameRing() {
    frameSlots_.clear();
    frameSlots_.resize(kFrameRingSize);
    for (u32 i = 0; i < kFrameRingSize; ++i) {
        const HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                                frameSlots_[i].fence.GetAddressOf());
        if (FAILED(hr)) { Fail("CreateFence", hr); return false; }
    }
    return true;
}

bool D3D12Backend::RecreateBufferPair(ComPtr<ID3D12Resource>& target, ComPtr<ID3D12Resource>& upload,
                                      void*& mapped, UINT64& capacityBytes, UINT64 requiredBytes) {
    if (target != nullptr && upload != nullptr && mapped != nullptr && capacityBytes >= requiredBytes)
        return true;

    // Growing means the old resources may still be referenced by in-flight
    // command lists. A dangling fence wait is the usual cause of "works for a while then
    // corrupts" bug in an upload-path D3D12 backend.
    if (!WaitForPendingFrames()) return false;

    const UINT64 bytes = AlignUp(requiredBytes, kBufferAlignment);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_HEAP_PROPERTIES uploadHeap = defaultHeap;
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> newTarget;
    HRESULT hr = device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   __uuidof(ID3D12Resource), newTarget.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateCommittedResource(default buffer)", hr); return false; }

    ComPtr<ID3D12Resource> newUpload;
    hr = device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          __uuidof(ID3D12Resource), newUpload.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateCommittedResource(upload buffer)", hr); return false; }

    void* newMapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    hr = newUpload->Map(0, &readRange, &newMapped);
    if (FAILED(hr)) { Fail("Map(upload buffer)", hr); return false; }
    std::memset(newMapped, 0, static_cast<size_t>(bytes));

    // Release the old pair only once the new one is fully built.
    target = newTarget;
    upload = newUpload;
    mapped = newMapped;
    capacityBytes = bytes;
    geometryDirty_ = true;
    return true;
}

bool D3D12Backend::CreateGeometryBuffers() {
    if (!RecreateBufferPair(vertexBuffer_, vertexUpload_, vertexMapped_, vertexCapacity_,
                            kInitialVertexBufferBytes))
        return false;
    if (!RecreateBufferPair(indexBuffer_, indexUpload_, indexMapped_, indexCapacity_,
                            kInitialIndexBufferBytes))
        return false;
    info_.vertexBufferSize = static_cast<u32>(vertexCapacity_);
    info_.indexBufferSize = static_cast<u32>(indexCapacity_);
    return true;
}

bool D3D12Backend::EnsureVertexCapacity(size_t vertices, size_t indices) {
    const UINT64 vertexBytes = static_cast<UINT64>(vertices) * sizeof(Vertex);
    const UINT64 indexBytes = static_cast<UINT64>(indices) * sizeof(u32);

    UINT64 wantVertices = vertexBuffer_ != nullptr ? vertexCapacity_ : kInitialVertexBufferBytes;
    UINT64 wantIndices = indexBuffer_ != nullptr ? indexCapacity_ : kInitialIndexBufferBytes;
    if (vertexBytes > wantVertices) wantVertices = Max<UINT64>(vertexBytes, wantVertices * 2ull);
    if (indexBytes > wantIndices) wantIndices = Max<UINT64>(indexBytes, wantIndices * 2ull);

    if (!RecreateBufferPair(vertexBuffer_, vertexUpload_, vertexMapped_, vertexCapacity_, wantVertices))
        return false;
    if (!RecreateBufferPair(indexBuffer_, indexUpload_, indexMapped_, indexCapacity_, wantIndices))
        return false;

    info_.vertexBufferSize = static_cast<u32>(vertexCapacity_);
    info_.indexBufferSize = static_cast<u32>(indexCapacity_);
    return true;
}

bool D3D12Backend::CreateStaticResources() {
    // Handle 1 / SRV slot 0: the built-in white 1x1 every untextured shape and
    // DrawCmd::textureId == 0 resolves to.
    const u32 white = 0xFFFFFFFFu;
    TextureDesc desc{};
    desc.width = 1;
    desc.height = 1;
    desc.format = TextureFormat::RGBA8;

    TextureRecord rec;
    if (!CreateTextureInternal(1u, desc, &white, rec)) return false;
    textures_.emplace(1u, std::move(rec));
    return true;
}

// Target

bool D3D12Backend::CreateSwapchainForWindow(u32 width, u32 height) {
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory4), factory.GetAddressOf());
    if (FAILED(hr) || factory == nullptr) { Fail("CreateDXGIFactory2", hr); return false; }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    // Premultiplied alpha is only legal with a BGRA8 chain and a non-flip
    // presentation model. The shader already outputs premultiplied colour, so
    // this is a pure format/alphamode decision.
    sd.Format = transparentBackground_ ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = msaaSamples_;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    // DISCARD keeps compatibility with the widest range of host windows and is
    // what most engines create, which matters when adopting a foreign chain.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.AlphaMode = transparentBackground_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;

    HWND hwnd = static_cast<HWND>(windowHandle_);

    // DXGI refuses to create a second swapchain for a window that already has
    // one associated with this factory. Detaching without changing any window
    // behaviour is the documented way through; the flags keep Alt+Enter away
    // from the host. The association is per-factory, and this factory is ours.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

    ComPtr<IDXGISwapChain1> chain1;
    hr = factory->CreateSwapChainForHwnd(queue_.Get(), &sd, hwnd, nullptr, chain1.GetAddressOf());
    if (FAILED(hr) || chain1 == nullptr) { Fail("CreateSwapChainForHwnd", hr); return false; }

    ComPtr<IDXGISwapChain3> chain3;
    hr = chain1.As(&chain3);
    if (FAILED(hr) || chain3 == nullptr) { Fail("QueryInterface(IDXGISwapChain3)", hr); return false; }

    swapChain_ = chain3;
    ownsSwapchain_ = true;
    info_.ownsSwapchain = true;
    return BuildTargetResources();
}

bool D3D12Backend::AdoptTarget(const TargetDesc& desc) {
    if (desc.externalSwapchain == nullptr) {
        lastError_ = "AdoptTarget was given a null swapchain";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (device_ == nullptr || queue_ == nullptr) {
        lastError_ = "AdoptTarget called before Initialize()";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    if (swapChain_ != nullptr) {
        lastError_ = "a swapchain is already attached; call DestroyTarget() first";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }

    ComPtr<IDXGISwapChain1> chain1;
    HRESULT hr = chain1.As(static_cast<IDXGISwapChain*>(desc.externalSwapchain));
    if (FAILED(hr) || chain1 == nullptr) { Fail("QueryInterface(IDXGISwapChain1)", hr); return false; }

    ComPtr<IDXGISwapChain3> chain3;
    hr = chain1.As(&chain3);
    if (FAILED(hr) || chain3 == nullptr) { Fail("QueryInterface(IDXGISwapChain3)", hr); return false; }

    swapChain_ = chain3;
    ownsSwapchain_ = false;
    info_.ownsSwapchain = false;
    ARGON_INFO(kTag, "adopted external swapchain %p", desc.externalSwapchain);

    // A host chain's format, buffer count and sample count are whatever the
    // host made them; TargetDesc::msaaSamples is advisory and ignored here.
    return BuildTargetResources();
}

bool D3D12Backend::CreateTarget(const TargetDesc& desc) {
    DestroyTarget();
    if (device_ == nullptr || queue_ == nullptr) {
        lastError_ = "CreateTarget called before Initialize()";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
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
    if (desc.msaaSamples > 0) msaaSamples_ = desc.msaaSamples;
    return CreateSwapchainForWindow(w, h);
}

bool D3D12Backend::BuildTargetResources() {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    HRESULT hr = swapChain_->GetDesc1(&sd);
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetDesc1", hr); return false; }

    targetFormat_ = sd.Format;
    targetSampleCount_ = sd.SampleDesc.Count > 0 ? sd.SampleDesc.Count : 1u;
    targetWidth_ = sd.BufferWidth;
    targetHeight_ = sd.BufferHeight;
    targetSize_ = Vec2(static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
    return BuildRenderTargets();
}

bool D3D12Backend::BuildRenderTargets() {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    HRESULT hr = swapChain_->GetDesc1(&sd);
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetDesc1", hr); return false; }

    const UINT bufferCount = sd.BufferCount > 0 ? sd.BufferCount : 1u;

    // One RTV per back buffer plus one slot for the optional MSAA target. The
    // heap is rebuilt on every resize because the buffer count may change.
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    hr = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, bufferCount + 1u, false,
                              &rtvHeap);
    if (FAILED(hr)) { Fail("CreateDescriptorHeap(RTV)", hr); return false; }

    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = rtvHeap->GetDescriptorHandleForHeapStart();
    const UINT rtvIncrement = rtvHeap->GetDescriptorIncrementSize();

    std::vector<ComPtr<ID3D12Resource>> backBuffers;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> backBufferRtv;
    backBuffers.reserve(bufferCount);
    backBufferRtv.reserve(bufferCount);

    for (UINT i = 0; i < bufferCount; ++i) {
        ComPtr<ID3D12Resource> buffer;
        hr = swapChain_->GetBuffer(i, __uuidof(ID3D12Resource), buffer.GetAddressOf());
        if (FAILED(hr) || buffer == nullptr) { Fail("IDXGISwapChain::GetBuffer", hr); return false; }

        D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += static_cast<SIZE_T>(i) * rtvIncrement;
        device_->CreateRenderTargetView(buffer.Get(), nullptr, handle);

        backBuffers.push_back(buffer);
        backBufferRtv.push_back(handle);
    }

    ComPtr<ID3D12Resource> msaa;
    D3D12_CPU_DESCRIPTOR_HANDLE msaaHandle{};

    if (targetSampleCount_ > 1) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = targetWidth_;
        desc.Height = targetHeight_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = targetFormat_;
        desc.SampleDesc.Count = targetSampleCount_;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_OPAQUE;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // Created directly in RENDER_TARGET (legal as an initial state, and what
        // Microsoft's own samples do for depth). The MSAA target is only ever
        // rendered into and resolved from, so it never needs a barrier and never
        // leaves this state.
        hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                              __uuidof(ID3D12Resource), msaa.GetAddressOf());
        if (FAILED(hr) || msaa == nullptr) {
            ARGON_WARN(kTag, "could not create a %ux MSAA target; falling back to single-sample",
                       targetSampleCount_);
            msaa.Reset();
            targetSampleCount_ = 1;  // the PSO's sample count must match what we bind
        } else {
            msaaHandle = heapStart;
            msaaHandle.ptr += static_cast<SIZE_T>(bufferCount) * rtvIncrement;
            device_->CreateRenderTargetView(msaa.Get(), nullptr, msaaHandle);
        }
    }

    if (!EnsurePipelines(targetFormat_, targetSampleCount_)) return false;

    // Commit everything at once: a partial rebuild would leave a half-valid
    // frame behind if any of the steps above fails.
    rtvHeap_ = rtvHeap;
    rtvHeapStart_ = heapStart;
    backBuffers_ = std::move(backBuffers);
    backBufferRtv_ = std::move(backBufferRtv);
    msaaTexture_ = msaa;
    msaaRtv_ = msaaHandle;
    return true;
}

bool D3D12Backend::EnsurePipelines(DXGI_FORMAT format, u32 sampleCount) {
    if (rootSignature_ == nullptr || vsBlob_ == nullptr || psBlob_ == nullptr) {
        lastError_ = "EnsurePipelines called before Initialize() completed";
        return false;
    }
    if (pipelineAlpha_ != nullptr && pipelineAdditive_ != nullptr && pipelineFormat_ == format &&
        pipelineSampleCount_ == sampleCount) {
        return true;
    }

    pipelineAlpha_.Reset();
    pipelineAdditive_.Reset();

    // Input layout - mirrors argon::ui::Vertex (64 bytes) exactly, and is kept
    // byte-for-byte identical to the D3D11 backend: if one changes, both must.
    const D3D12_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION",  0, DXGI_FORMAT_R32G32_FLOAT,        0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,        8,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  1, DXGI_FORMAT_R32G32_FLOAT,        16,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  2, DXGI_FORMAT_R32G32B32A32_FLOAT, 24,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  3, DXGI_FORMAT_R32G32_FLOAT,        40,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  4, DXGI_FORMAT_R32_UINT,            48,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  5, DXGI_FORMAT_R32_UINT,            52,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  6, DXGI_FORMAT_R32_UINT,            56,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  7, DXGI_FORMAT_R32_UINT,            60,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS.pShaderBytecode = vsBlob_->GetBufferPointer();
    pso.VS.BytecodeLength = vsBlob_->GetBufferSize();
    pso.PS.pShaderBytecode = psBlob_->GetBufferPointer();
    pso.PS.BytecodeLength = psBlob_->GetBufferSize();

    // Premultiplied alpha: the shader returns rgb * a, so the blend is
    // (ONE, ONE_MINUS_SRC_ALPHA). Matches the reference UIs exactly.
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;   // the SDF is winding-independent
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    pso.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    pso.InputLayout.pInputElementDescs = elements;
    pso.InputLayout.NumElements = ARRAYSIZE(elements);
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = format;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = sampleCount;
    pso.SampleDesc.Quality = 0;

    HRESULT hr = device_->CreateGraphicsPipelineState(&pso, __uuidof(ID3D12PipelineState),
                                                       pipelineAlpha_.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateGraphicsPipelineState(alpha)", hr); return false; }

    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState = blend;

    hr = device_->CreateGraphicsPipelineState(&pso, __uuidof(ID3D12PipelineState),
                                              pipelineAdditive_.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateGraphicsPipelineState(additive)", hr); return false; }

    pipelineFormat_ = format;
    pipelineSampleCount_ = sampleCount;
    return true;
}

bool D3D12Backend::ResizeTarget(u32 width, u32 height) {
    if (swapChain_ == nullptr) return false;
    if (width == 0 || height == 0) return true;              // minimised
    if (width == targetWidth_ && height == targetHeight_) return true;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    HRESULT hr = swapChain_->GetDesc1(&sd);
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetDesc1", hr); return false; }

    // Two things must be true before ResizeBuffers: no live reference to a back
    // buffer (the usual cause of a black overlay) and no in-flight frame
    // that still uses them.
    if (!WaitForPendingFrames()) return false;
    backBuffers_.clear();
    backBufferRtv_.clear();
    msaaTexture_.Reset();
    rtvHeap_.Reset();

    // A brand-new descriptor rather than ResizeBuffers(0, ...): D3D12 cannot
    // change the sample description after creation, and only the fields we pass
    // explicitly are honoured. Width, height and the buffer count are the only
    // things that may legitimately move.
    const UINT bufferCount = sd.BufferCount > 0 ? sd.BufferCount : 2u;
    hr = swapChain_->ResizeBuffers(bufferCount, width, height, sd.Format, sd.Flags);
    if (FAILED(hr)) {
        Fail("IDXGISwapChain::ResizeBuffers", hr);
        if (hr == DXGI_ERROR_INVALID_CALL) {
            lastError_ += " - a back-buffer reference is still held somewhere";
        }
        // The old buffers are still valid; put a usable target back so the next
        // frame is not dropped silently forever.
        BuildTargetResources();
        return false;
    }
    // Re-reads the (now updated) descriptor, so the MSAA target and the RTV
    // heap are rebuilt at the new size rather than the old one.
    return BuildTargetResources();
}

void D3D12Backend::DestroyTarget() {
    pipelineAlpha_.Reset();
    pipelineAdditive_.Reset();
    pipelineFormat_ = DXGI_FORMAT_UNKNOWN;
    pipelineSampleCount_ = 0;

    msaaTexture_.Reset();
    msaaRtv_ = D3D12_CPU_DESCRIPTOR_HANDLE{};
    backBufferRtv_.clear();
    backBuffers_.clear();
    rtvHeap_.Reset();
    rtvHeapStart_ = D3D12_CPU_DESCRIPTOR_HANDLE{};

    if (swapChain_ != nullptr) {
        // An adopted chain is not ours to release. Detach() drops our reference
        // without calling Release() on the host's object.
        if (!ownsSwapchain_) swapChain_.Detach();
        swapChain_.Reset();
    }
    ownsSwapchain_ = false;
    info_.ownsSwapchain = false;

    targetWidth_ = targetHeight_ = 0;
    targetSampleCount_ = 1;
    targetFormat_ = DXGI_FORMAT_UNKNOWN;
    targetSize_ = Vec2(0.0f, 0.0f);
}

// Textures

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::SrvHandle(u32 handle) const {
    UINT slot = 0;  // the built-in white 1x1
    if (handle > 1) {
        auto it = textures_.find(handle);
        // An unknown handle degrades to white rather than to a dangling
        // descriptor - same fallback the D3D11 backend's ResolveSrv performs.
        slot = (it == textures_.end()) ? 0u : (handle - 1u);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE result = srvHeapStart_;
    result.ptr += static_cast<SIZE_T>(slot) * srvIncrement_;
    return result;
}

bool D3D12Backend::RunTextureCommands(const D3D12_TEXTURE_COPY_LOCATION* dst,
                                      const D3D12_TEXTURE_COPY_LOCATION* src, UINT width, UINT height,
                                      ID3D12Resource* barrierResource,
                                      D3D12_RESOURCE_STATE from, D3D12_RESOURCE_STATE to) {
    if (queue_ == nullptr) return false;

    if (copyAllocator_ == nullptr) {
        const HRESULT hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            __uuidof(ID3D12CommandAllocator),
                                                            copyAllocator_.GetAddressOf());
        if (FAILED(hr)) { Fail("CreateCommandAllocator(upload)", hr); return false; }
    }
    if (copyList_ == nullptr) {
        HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                copyAllocator_.Get(), nullptr,
                                                __uuidof(ID3D12GraphicsCommandList),
                                                copyList_.GetAddressOf());
        if (FAILED(hr)) { Fail("CreateCommandList(upload)", hr); return false; }
        hr = copyList_->Close();
        if (FAILED(hr)) { Fail("GraphicsCommandList::Close(upload, initial)", hr); return false; }
    }

    HRESULT hr = copyAllocator_->Reset();
    if (FAILED(hr)) { Fail("CommandAllocator::Reset(upload)", hr); return false; }
    hr = copyList_->Reset(copyAllocator_.Get(), nullptr);
    if (FAILED(hr)) { Fail("GraphicsCommandList::Reset(upload)", hr); return false; }

    if (dst != nullptr && src != nullptr && dst->pResource != nullptr && src->pResource != nullptr) {
        // CopyTextureRegion takes a non-const destination and a const source.
        // The source here is a *buffer*: a tightly packed upload buffer needs no
        // footprint query, because the copy reads width * height * bpp from 0.
        D3D12_TEXTURE_COPY_LOCATION dest = *dst;
        copyList_->CopyTextureRegion(&dest, 0, 0, 0, width, height, 1, src, nullptr);
    }
    if (barrierResource != nullptr) {
        const D3D12_RESOURCE_BARRIER barrier = MakeTransition(barrierResource, from, to);
        copyList_->ResourceBarrier(1, &barrier);
    }

    hr = copyList_->Close();
    if (FAILED(hr)) { Fail("GraphicsCommandList::Close(upload)", hr); return false; }

    ID3D12CommandList* lists[1] = { copyList_.Get() };
    queue_->ExecuteCommandLists(1, lists);
    return true;
}

bool D3D12Backend::UploadTextureData(const TextureDesc& desc, const void* pixels, TextureRecord& rec) {
    if (rec.staging == nullptr || pixels == nullptr) return false;

    // The staging buffer is CPU memory the GPU may still be reading from a
    // previous upload, so drain before overwriting it. Uploads happen on the
    // font-atlas / image-load path, never per frame, so the stall is free.
    if (!WaitForPendingFrames()) return false;

    const size_t rowBytes = static_cast<size_t>(desc.width) * BytesPerTexel(desc.format);

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    HRESULT hr = rec.staging->Map(0, &readRange, &mapped);
    if (FAILED(hr)) { Fail("Map(texture upload)", hr); return false; }

    // Row by row: the staging buffer is tightly packed, so its row pitch is
    // exactly width * bytes-per-texel, but the caller's source rows need not be.
    const u8* src = static_cast<const u8*>(pixels);
    u8* dst = static_cast<u8*>(mapped);
    for (u32 y = 0; y < desc.height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * rowBytes,
                    src + static_cast<size_t>(y) * rowBytes, rowBytes);
    }
    rec.staging->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = rec.texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = rec.staging.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    // The copy leaves the destination in PIXEL_SHADER_RESOURCE, which is the
    // only state ArgonUI ever samples a texture in.
    return RunTextureCommands(&dstLoc, &srcLoc, desc.width, desc.height, rec.texture.Get(),
                              D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool D3D12Backend::CreateTextureInternal(u32 handle, const TextureDesc& desc, const void* pixels,
                                         TextureRecord& out) {
    if (srvHeap_ == nullptr || queue_ == nullptr) {
        lastError_ = "CreateTexture called before Initialize()";
        return false;
    }
    if (desc.width == 0 || desc.height == 0) return false;
    if (handle < 1 || handle > kMaxSrvDescriptors) return false;

    out.desc = desc;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = desc.width;
    texDesc.Height = desc.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = ToDxgiFormat(desc.format);
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_OPAQUE;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // Created in COPY_DEST, which is the only safe initial state for a resource
    // the upload path is about to write.
    HRESULT hr = device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  __uuidof(ID3D12Resource), out.texture.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateCommittedResource(texture)", hr); return false; }

    // Tightly packed upload buffer: width * height * bytes-per-texel, which is
    // exactly the region CopyTextureRegion reads from an unpadded source.
    D3D12_HEAP_PROPERTIES uploadHeap = defaultHeap;
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = AlignUp(static_cast<UINT64>(desc.width) * desc.height * BytesPerTexel(desc.format),
                               kBufferAlignment);
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          __uuidof(ID3D12Resource), out.staging.GetAddressOf());
    if (FAILED(hr)) { Fail("CreateCommittedResource(texture upload)", hr); return false; }

    // The SRV descriptor is written once, here, into the handle's permanent
    // slot. Nothing else in the backend ever needs to touch it again.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = ToDxgiFormat(desc.format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device_->CreateShaderResourceView(out.texture.Get(), &srvDesc, SrvHandle(handle));

    if (pixels != nullptr) {
        if (!UploadTextureData(desc, pixels, out)) return false;
    } else if (!RunTextureCommands(nullptr, nullptr, 0, 0, out.texture.Get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
        // Nothing to copy, but the resource still has to leave COPY_DEST or the
        // very first sample is a validation error.
        return false;
    }
    return true;
}

u32 D3D12Backend::CreateTexture(const TextureDesc& desc, const void* pixels) {
    if (device_ == nullptr || srvHeap_ == nullptr) return 0;
    if (desc.width == 0 || desc.height == 0) return 0;
    if (nextTextureId_ > kMaxSrvDescriptors) {
        lastError_ = "texture descriptor heap is full";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return 0;
    }

    const u32 handle = nextTextureId_;
    TextureRecord rec;
    if (!CreateTextureInternal(handle, desc, pixels, rec)) {
        nextTextureId_ = handle;  // the id was never published
        return 0;
    }
    nextTextureId_ = handle + 1u;
    textures_.emplace(handle, std::move(rec));
    maxTextureSize_ = Max(maxTextureSize_, Max(desc.width, desc.height));
    info_.maxTextureSize = maxTextureSize_;
    return handle;
}

bool D3D12Backend::UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) {
    auto it = textures_.find(handle);
    if (it == textures_.end() || pixels == nullptr) return false;

    if (desc.width != it->second.desc.width || desc.height != it->second.desc.height ||
        desc.format != it->second.desc.format) {
        // A format or size change needs a full re-create; the descriptor keeps
        // its slot, so only the resource behind it changes.
        TextureRecord fresh;
        if (!CreateTextureInternal(handle, desc, pixels, fresh)) return false;
        it->second = std::move(fresh);
        return true;
    }
    return UploadTextureData(desc, pixels, it->second);
}

void D3D12Backend::DestroyTexture(u32 handle) {
    if (handle <= 1) return;  // the white 1x1 is not ours to free here
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;

    // Blank the slot. The heap outlives every texture, and a descriptor left
    // pointing at a destroyed resource is a dangling view that the debug layer
    // (and a future allocator) would happily read.
    if (device_ != nullptr && srvHeap_ != nullptr) device_->CreateNullDescriptor(SrvHandle(handle));

    textures_.erase(it);
}

// Frame

bool D3D12Backend::WaitForFence(FrameSlot& slot) {
    if (slot.fence == nullptr) return true;
    if (slot.fenceValue == 0) return true;  // nothing has ever been signalled here

    const u64 target = slot.fenceValue - 1;
    if (slot.fence->GetCompletedValue() >= target) {
        slot.fenceValue = 0;
        return true;
    }

    // A fence object is a waitable kernel object: this returns once the GPU has
    // reached `target`, i.e. once every list submitted before that signal has
    // retired. No event handle, no spinning.
    const DWORD r = ::WaitForSingleObject(reinterpret_cast<HANDLE>(slot.fence.Get()),
                                          static_cast<DWORD>(target));
    slot.fenceValue = 0;
    if (r != WAIT_OBJECT_0) {
        lastError_ = "fence wait failed";
        ARGON_ERROR(kTag, "%s", lastError_.c_str());
        return false;
    }
    return true;
}

bool D3D12Backend::WaitForPendingFrames() {
    bool ok = true;
    for (size_t i = 0; i < frameSlots_.size(); ++i) {
        if (!WaitForFence(frameSlots_[i])) ok = false;
    }
    return ok;
}

bool D3D12Backend::BeginFrame(const FrameState& state) {
    if (device_ == nullptr || queue_ == nullptr) return false;
    if (frameBegun_) return false;                          // unbalanced Begin/End
    if (frameSlots_.empty()) return false;
    if (backBuffers_.empty() || rtvHeap_ == nullptr) return false;  // no RTV
    if (state.occluded) return false;                       // minimised / hidden

    frameSlot_ = static_cast<u32>(frameCounter_ % frameSlots_.size());
    FrameSlot& slot = frameSlots_[frameSlot_];

    // Two waits, two hazards.
    //
    //  * This slot's allocator and command list cannot be Reset() while the
    //    frame that last used them is still on the GPU - that is what the ring
    //    depth is for.
    //  * The upload buffers and the viewport constant buffer are mapped once
    //    and written with memcpy. Overwriting them while the GPU is still
    //    reading is a silent data race that shows up as garbage geometry a few
    //    frames later, so the *previous* frame's fence has to be drained too.
    //    That also drains everything older, because the queue is in order.
    const u32 prevSlot = static_cast<u32>((frameSlot_ + frameSlots_.size() - 1) % frameSlots_.size());
    if (!WaitForFence(frameSlots_[prevSlot])) return false;
    if (!WaitForFence(slot)) return false;

    // Allocators and lists are created once per slot and reset every frame;
    // creating them per frame is pure churn.
    if (slot.allocator == nullptr) {
        const HRESULT hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            __uuidof(ID3D12CommandAllocator),
                                                            slot.allocator.GetAddressOf());
        if (FAILED(hr)) { Fail("CreateCommandAllocator", hr); return false; }
    }
    if (slot.list == nullptr) {
        HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(),
                                                nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                slot.list.GetAddressOf());
        if (FAILED(hr)) { Fail("CreateCommandList", hr); return false; }
        // A freshly created list is in the recording state; close it so it can be
        // Reset() cleanly next frame.
        hr = slot.list->Close();
        if (FAILED(hr)) { Fail("GraphicsCommandList::Close(initial)", hr); return false; }
    }

    HRESULT hr = slot.allocator->Reset();
    if (FAILED(hr)) { Fail("CommandAllocator::Reset", hr); return false; }
    hr = slot.list->Reset(slot.allocator.Get(), nullptr);
    if (FAILED(hr)) { Fail("GraphicsCommandList::Reset", hr); return false; }

    frameBegun_ = true;
    workSubmitted_ = false;
    drawCallCount_ = 0;
    vertexBytesUploaded_ = 0;
    indexBytesUploaded_ = 0;
    indexCountUploaded_ = 0;
    ++frameCounter_;
    return true;
}

bool D3D12Backend::SubmitRecordedWork() {
    if (workSubmitted_) return true;
    FrameSlot& slot = frameSlots_[frameSlot_];
    if (slot.list == nullptr) return true;

    workSubmitted_ = true;

    HRESULT hr = slot.list->Close();
    if (FAILED(hr)) {
        Fail("GraphicsCommandList::Close", hr);
        return false;
    }

    // Only ever executed on a list this backend created. The host's own list is
    // never touched. That is the entire contract of DeviceSource::External.
    ID3D12CommandList* lists[1] = { slot.list.Get() };
    queue_->ExecuteCommandLists(1, lists);

    // Signal *after* execution, so the fence value means "this frame is done",
    // not "this frame was queued".
    hr = queue_->Signal(slot.fence.Get(), slot.fenceValue);
    if (FAILED(hr)) { Fail("ID3D12CommandQueue::Signal", hr); return false; }
    ++slot.fenceValue;
    return true;
}

void D3D12Backend::Render(const DrawList& list, const FrameState&) {
    if (!frameBegun_ || device_ == nullptr || queue_ == nullptr) return;
    if (backBuffers_.empty() || rtvHeap_ == nullptr) return;

    FrameSlot& slot = frameSlots_[frameSlot_];
    ID3D12GraphicsCommandList* cmd = slot.list.Get();
    if (cmd == nullptr || pipelineAlpha_ == nullptr || rootSignature_ == nullptr) return;
    if (list.CmdCount() == 0) return;

    const UINT width = targetWidth_;
    const UINT height = targetHeight_;
    const UINT currentIndex = swapChain_->GetCurrentBackBufferIndex();

    // -- Upload ------------------------------------------------------------
    if (!EnsureVertexCapacity(list.VertexCount(), list.IndexCount())) return;

    // Upload the whole arrays, exactly as the D3D11 backend does, and record
    // how many bytes actually went in: the copy below and the draw bounds check
    // both key off these.
    vertexBytesUploaded_ = static_cast<u64>(list.VertexCount()) * sizeof(Vertex);
    indexBytesUploaded_ = static_cast<u64>(list.IndexCount()) * sizeof(u32);
    indexCountUploaded_ = list.IndexCount();
    if (vertexBytesUploaded_ == 0 || indexBytesUploaded_ == 0) return;

    // Safe without a barrier: BeginFrame drained the GPU, and nothing else on
    // this queue touches these bytes until the copy below is recorded.
    std::memcpy(vertexMapped_, list.Vertices(), static_cast<size_t>(vertexBytesUploaded_));
    std::memcpy(indexMapped_, list.Indices(), static_cast<size_t>(indexBytesUploaded_));

    // A DEFAULT resource may sit in exactly one state. Ours are only ever read,
    // so they transition once after creation (or after a growth) and
    // never leave it.
    if (geometryDirty_) {
        D3D12_RESOURCE_BARRIER barriers[2];
        barriers[0] = MakeTransition(vertexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        barriers[1] = MakeTransition(indexBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_INDEX_BUFFER);
        cmd->ResourceBarrier(2, barriers);
        geometryDirty_ = false;
    }

    // One copy per resource. The upload buffers need no transition of their own:
    // an UPLOAD heap resource is implicitly in COPY_DEST for the whole of its
    // (create, destroy) life.
    cmd->CopyBufferRegion(vertexBuffer_.Get(), 0, vertexUpload_.Get(), 0, vertexBytesUploaded_);
    cmd->CopyBufferRegion(indexBuffer_.Get(), 0, indexUpload_.Get(), 0, indexBytesUploaded_);

    // -- Render targets ----------------------------------------------------
    // DXGI owns the back buffers: they arrive in PRESENT state on creation and
    // are back in PRESENT state after every Present, so this pair of transitions
    // is unconditional and symmetric - every buffer leaves the frame in the
    // state the next frame expects to find it in.
    std::vector<D3D12_RESOURCE_BARRIER> toTarget;
    std::vector<D3D12_RESOURCE_BARRIER> toPresent;
    toTarget.reserve(backBuffers_.size());
    toPresent.reserve(backBuffers_.size());
    for (size_t i = 0; i < backBuffers_.size(); ++i) {
        toTarget.push_back(MakeTransition(backBuffers_[i].Get(),
                                          D3D12_RESOURCE_STATE_PRESENT,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET));
        toPresent.push_back(MakeTransition(backBuffers_[i].Get(),
                                           D3D12_RESOURCE_STATE_RENDER_TARGET,
                                           D3D12_RESOURCE_STATE_PRESENT));
    }
    cmd->ResourceBarrier(static_cast<UINT>(toTarget.size()), toTarget.data());

    // The MSAA target was created in RENDER_TARGET and never leaves it.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
    UINT rtvCount = 0;
    const size_t rtvIndex =
        (static_cast<size_t>(currentIndex) < backBufferRtv_.size()) ? currentIndex : 0;
    if (msaaTexture_ != nullptr) {
        // With MSAA the back buffer is the *resolve* destination: it has to be
        // bound too, or the resolve goes nowhere.
        rtvs[0] = msaaRtv_;
        rtvs[1] = backBufferRtv_[rtvIndex];
        rtvCount = 2;
    } else {
        rtvs[0] = backBufferRtv_[rtvIndex];
        rtvCount = 1;
    }
    cmd->OMSetRenderTargets(rtvCount, rtvs, FALSE, nullptr);

    // -- Pipeline / heaps --------------------------------------------------
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineAlpha_.Get());

    // Order matters: non-root heaps first, root heap last.
    ID3D12DescriptorHeap* heaps[2] = { srvHeap_.Get(), rootDescriptorHeap_.Get() };
    cmd->SetDescriptorHeaps(2, heaps);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<f32>(width);
    viewport.Height = static_cast<f32>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    cmd->RSSetViewports(1, &viewport);

    // The shader turns pixel coordinates into clip space via this constant.
    {
        const f32 values[4] = {
            static_cast<f32>(width), static_cast<f32>(height),
            1.0f / static_cast<f32>(Max(1u, width)),
            1.0f / static_cast<f32>(Max(1u, height))
        };
        std::memset(viewportMapped_, 0, kViewportBufferBytes);
        std::memcpy(viewportMapped_, values, sizeof(values));
    }
    cmd->SetGraphicsRootConstantBufferView(0, viewportGpuAddress_);

    // -- Draw --------------------------------------------------------------
    ID3D12PipelineState* boundPipeline = pipelineAlpha_.Get();
    D3D12_CPU_DESCRIPTOR_HANDLE boundSrv{};
    bool srvBound = false;
    D3D12_RECT boundScissor{};
    bool scissorBound = false;

    const DrawCmd* cmds = list.Cmds();
    const u32 cmdCount = list.CmdCount();

    for (u32 i = 0; i < cmdCount; ++i) {
        const DrawCmd& drawCmd = cmds[i];
        if (drawCmd.indexCount == 0) continue;
        // Defensive: never let a malformed DrawList make the GPU read past the
        // indices we uploaded this frame.
        if (drawCmd.indexOffset + drawCmd.indexCount > indexCountUploaded_) continue;

        // Scissor: clamp the CPU-side clip to the target, floor/ceil so partial
        // pixel coverage is never cut off.
        D3D12_RECT scissor{};
        const f32 l = Max(0.0f, std::floor(drawCmd.clip.min.x));
        const f32 t = Max(0.0f, std::floor(drawCmd.clip.min.y));
        const f32 r = Min(static_cast<f32>(width), std::ceil(drawCmd.clip.max.x));
        const f32 b = Min(static_cast<f32>(height), std::ceil(drawCmd.clip.max.y));
        scissor.left = static_cast<LONG>(l);
        scissor.top = static_cast<LONG>(t);
        scissor.right = static_cast<LONG>(Max(l + 1.0f, r));
        scissor.bottom = static_cast<LONG>(Max(t + 1.0f, b));

        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;

        ID3D12PipelineState* wantPipeline =
            (drawCmd.blend == BlendMode::Additive) ? pipelineAdditive_.Get() : pipelineAlpha_.Get();
        if (wantPipeline != boundPipeline) {
            cmd->SetPipelineState(wantPipeline);
            boundPipeline = wantPipeline;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE wantSrv = SrvHandle(drawCmd.textureId);
        if (!srvBound || wantSrv.ptr != boundSrv.ptr) {
            cmd->SetGraphicsRootDescriptorTable(1, wantSrv);
            boundSrv = wantSrv;
            srvBound = true;
        }

        if (!scissorBound || boundScissor.left != scissor.left || boundScissor.top != scissor.top ||
            boundScissor.right != scissor.right || boundScissor.bottom != scissor.bottom) {
            cmd->RSSetScissorRects(1, &scissor);
            boundScissor = scissor;
            scissorBound = true;
        }

        // Indices are absolute into the frame's index array, so the base vertex
        // stays at 0 and the start location is the batch's offset.
        cmd->DrawIndexedInstanced(drawCmd.indexCount, 1, drawCmd.indexOffset, 0, 0);
        ++drawCallCount_;
    }

    // Back to PRESENT, ready for DXGI.
    cmd->ResourceBarrier(static_cast<UINT>(toPresent.size()), toPresent.data());
}

bool D3D12Backend::Present() {
    if (!frameBegun_) return false;

    // The canonical call order is Render -> Present -> EndFrame, so the list has
    // to be submitted here. EndFrame() calls the same helper and is a no-op
    // afterwards.
    if (!SubmitRecordedWork()) return false;

    if (!ownsSwapchain_ || swapChain_ == nullptr) return false;  // adopted: the host presents

    const HRESULT hr = swapChain_->Present(syncInterval_, 0);
    if (FAILED(hr)) Fail("IDXGISwapChain::Present", hr);
    return SUCCEEDED(hr);
}

void D3D12Backend::EndFrame() {
    if (!frameBegun_) return;

    // Submits if Present() did not already. Safe to call when BeginFrame failed:
    // the guard above covers that path.
    SubmitRecordedWork();

    frameBegun_ = false;
    workSubmitted_ = false;
}

IArgonBackend* CreateD3D12Backend() { return new D3D12Backend(); }

} // namespace argon::ui
