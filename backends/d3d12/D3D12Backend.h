// ArgonUI - backends/d3d12/D3D12Backend.h
//
// The D3D12 sibling of backends/d3d11/D3D11Backend.h. Same DrawList in, same
// pixels out; everything underneath is different, and the differences are the
// whole reason this file is longer:
//
//   * There is no immediate context. Drawing means filling a command list and
//     handing it to a queue, so this backend owns a ring of (allocator, list,
//     fence) triples and a Present-time submission.
//   * There is no implicit state. Every resource this backend touches needs an
//     explicit barrier, and the swapchain's back buffers are only guaranteed
//     to be in D3D12_RESOURCE_STATE_PRESENT.
//   * There is no free Map(). A DEFAULT buffer cannot be mapped, and an UPLOAD
//     buffer cannot be resized. The upload buffers below are therefore created
//     once, mapped once, and written with plain memcpy for the lifetime of the
//     backend - growing them re-creates the pair.
//   * Shaders no longer carry their bindings. The root signature, the descriptor
//     heaps and the static sampler are part of the pipeline state now.
//
// Two ownership traps are worth spelling out because they are silent:
//   * A ComPtr releases on destruction. Adopted host objects must be Detach()ed
//     instead of Reset(), or Shutdown() destroys somebody else's device.
//   * A persistently mapped upload buffer is CPU memory the GPU may still be
//     reading. BeginFrame() waits on the previous frame's fence before touching
//     it; that wait is not optional.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "../common/ComPtr.h"
#include "argonui/Backend/Backend.h"

namespace argon::ui {
class D3D12Backend final : public IArgonBackend {
public:
    D3D12Backend();
    ~D3D12Backend() override;

    // -- IArgonBackend -----------------------------------------------------
    bool Initialize(const DeviceDesc& desc) override;
    void Shutdown() override;

    bool CreateTarget(const TargetDesc& desc) override;
    bool AdoptTarget(const TargetDesc& desc) override;
    bool ResizeTarget(u32 width, u32 height) override;
    void DestroyTarget() override;
    bool HasTarget() const override { return !backBuffers_.empty() && rtvHeap_ != nullptr; }
    Vec2 TargetSize() const override { return targetSize_; }

    bool BeginFrame(const FrameState& state) override;
    void Render(const DrawList& list, const FrameState& state) override;
    bool Present() override;
    void EndFrame() override;

    u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) override;
    bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) override;
    void DestroyTexture(u32 handle) override;

    const BackendInfo& Info() const override { return info_; }
    const char* Name() const override { return "D3D12"; }
    const char* LastError() const override { return lastError_.c_str(); }

    // -- Escape hatches for hosts that interleave their own draws ---------
    ID3D12Device* Device() const { return device_.Get(); }
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    IDXGISwapChain3* SwapChain() const { return swapChain_.Get(); }
    /// RTV descriptor handle of back buffer 0 of the *current* back buffer.
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetHandle() const;
    u32 DrawCallCount() const { return drawCallCount_; }

private:
    // Ring depth. Two in flight is the arithmetic minimum for reusing an
    // allocator; three leaves a slot of slack for a frame that was skipped.
    static constexpr u32 kFrameRingSize = 3;

    /// One white 1x1 plus every host texture ArgonUI will ever be asked to
    /// hold. The heap is created once; SRV descriptors are written into slots.
    static constexpr UINT kMaxSrvDescriptors = 4096;

    /// Slot reserved for the 1x1 transparent texture that freed descriptors
    /// get repointed at, so they never reference a destroyed resource.
    static constexpr u32 kDeadSrvHandle = kMaxSrvDescriptors;

    // D3D12 fixes descriptor sizes in the specification, so there is no need
    // to ask the driver: CBV/SRV/UAV/sampler are 256 bytes, RTV/DSV 32.
    static constexpr UINT kDescriptorStride = 256;
    static constexpr UINT kRtvDescriptorStride = 32;

    static constexpr UINT64 kInitialVertexBufferBytes = 2ull * 1024ull * 1024ull;
    static constexpr UINT64 kInitialIndexBufferBytes = 1536ull * 1024ull;

    /// gViewport is one float4, but D3D12 constant buffer offsets and sizes are
    /// quantised to 256 bytes, so the resource is exactly one CBV slot.
    static constexpr UINT kViewportBufferBytes = 256;
static constexpr UINT kSamplerBufferBytes = 256;  // one D3D12_SAMPLER_DESC, CBV-aligned

    struct TextureRecord {
        ComPtr<ID3D12Resource> texture;
        /// Tightly packed upload buffer, width * height * bytes-per-texel.
        ComPtr<ID3D12Resource> staging;
        TextureDesc desc{};
    };

    struct FrameSlot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        /// Next value we will signal on this slot's fence. 0 means "nothing has
        /// been signalled yet", so waiting on it is a no-op.
        u64 fenceValue = 0;
    };

    // -- Setup -------------------------------------------------------------
    bool CompileShaders();
    bool CreateRootSignature();
    bool CreateStaticDescriptorHeaps();
    bool CreateViewportConstantBuffer();
bool CreateSamplerConstantBuffer();
    bool CreateFrameRing();
    bool CreateGeometryBuffers();
    bool CreateStaticResources();

    // -- Target ------------------------------------------------------------
    bool CreateSwapchainForWindow(u32 width, u32 height);
    bool BuildTargetResources();
    bool BuildRenderTargets();
    bool EnsurePipelines(DXGI_FORMAT format, u32 sampleCount);

    // -- Geometry ----------------------------------------------------------
    bool EnsureVertexCapacity(size_t vertices, size_t indices);
    bool RecreateBufferPair(ComPtr<ID3D12Resource>& target, ComPtr<ID3D12Resource>& upload,
                            void*& mapped, UINT64& capacityBytes, UINT64 requiredBytes);

    // -- Textures ----------------------------------------------------------
    bool CreateTextureInternal(u32 handle, const TextureDesc& desc, const void* pixels,
                               TextureRecord& out);
    bool UploadTextureData(const TextureDesc& desc, const void* pixels, TextureRecord& rec);
    bool RunTextureCommands(const D3D12_TEXTURE_COPY_LOCATION* dst,
                            const D3D12_TEXTURE_COPY_LOCATION* src, UINT width, UINT height,
                            ID3D12Resource* barrierResource,
                            D3D12_RESOURCE_STATE from, D3D12_RESOURCE_STATE to);
    D3D12_CPU_DESCRIPTOR_HANDLE SrvHandle(u32 handle) const;
    /// Root descriptor tables take the GPU handle for the same descriptor.
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(u32 handle) const;
    /// Maps a texture handle to its descriptor slot. Handles and slots are not
    /// the same number: slot 0 is always the built-in white 1x1 and slot 1 is the
    /// reserved dead texture.
    UINT ClaimSrvSlot(u32 handle);

    // -- Frame -------------------------------------------------------------
    bool WaitForPendingFrames();
    bool WaitForFence(FrameSlot& slot);
    bool SubmitRecordedWork();

    void Fail(const char* what, long hr);

    // -- Device / target ---------------------------------------------------
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    std::vector<ComPtr<ID3D12Resource>> backBuffers_;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> backBufferRtv_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12Resource> msaaTexture_;
    D3D12_CPU_DESCRIPTOR_HANDLE msaaRtv_{};

    bool ownsDevice_ = false;
    bool ownsQueue_ = false;
    bool ownsSwapchain_ = false;
    bool transparentBackground_ = false;
    void* windowHandle_ = nullptr;
    UINT syncInterval_ = 1;
    UINT msaaSamples_ = 1;

    u32 targetWidth_ = 0;
    u32 targetHeight_ = 0;
    u32 targetSampleCount_ = 1;
    DXGI_FORMAT targetFormat_ = DXGI_FORMAT_UNKNOWN;
    Vec2 targetSize_{0.0f, 0.0f};

    // -- Pipeline ----------------------------------------------------------
    ComPtr<ID3DBlob> vsBlob_;
    ComPtr<ID3DBlob> psBlob_;
    ComPtr<ID3DBlob> vsMessage_;
    ComPtr<ID3DBlob> psMessage_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineAlpha_;
    ComPtr<ID3D12PipelineState> pipelineAdditive_;
    /// A PSO is baked with a render target format and a sample count, so it has
    /// to be rebuilt whenever the swapchain changes either of them.
    DXGI_FORMAT pipelineFormat_ = DXGI_FORMAT_UNKNOWN;
    u32 pipelineSampleCount_ = 0;

    // -- Descriptors -------------------------------------------------------
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12DescriptorHeap> rootDescriptorHeap_;   ///< one CBV: gViewport
    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapStart_{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvHeapGpuStart_{};
    std::unordered_map<u32, UINT> srvSlot_;
    UINT nextSrvSlot_ = 0;
    ComPtr<ID3D12Resource> deadTexture_;  ///< 1x1 transparent, at kDeadSrvHandle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapStart_{};
    UINT srvIncrement_ = 0;

    // -- Per-frame uniform + geometry -------------------------------------
    ComPtr<ID3D12Resource> viewportBuffer_;
    D3D12_GPU_VIRTUAL_ADDRESS viewportGpuAddress_ = 0;
    void* viewportMapped_ = nullptr;

    ComPtr<ID3D12Resource> samplerBuffer_;
    D3D12_GPU_VIRTUAL_ADDRESS samplerGpuAddress_ = 0;
    void* samplerMapped_ = nullptr;

    ComPtr<ID3D12Resource> vertexBuffer_;
    ComPtr<ID3D12Resource> vertexUpload_;
    void* vertexMapped_ = nullptr;
    UINT64 vertexCapacity_ = 0;

    ComPtr<ID3D12Resource> indexBuffer_;
    ComPtr<ID3D12Resource> indexUpload_;
    void* indexMapped_ = nullptr;
    UINT64 indexCapacity_ = 0;

    /// A freshly created DEFAULT resource starts in COPY_DEST and has to be
    /// transitioned into its read state exactly once before the first draw.
    bool geometryDirty_ = true;

    /// Bytes / element count pushed into the upload buffers this frame. The
    /// draw loop bounds-checks index offsets against `indexCountUploaded_`.
    u64 vertexBytesUploaded_ = 0;
    u64 indexBytesUploaded_ = 0;
    u32 indexCountUploaded_ = 0;

    // -- Textures ----------------------------------------------------------
    std::unordered_map<u32, TextureRecord> textures_;
    u32 nextTextureId_ = 2;  // id 1 is the built-in white 1x1
    /// Private one-shot list used for texture uploads, which happen outside the
    /// frame loop (font atlas rebuilds, image loads).
    ComPtr<ID3D12CommandAllocator> copyAllocator_;
    ComPtr<ID3D12GraphicsCommandList> copyList_;

    // -- Frame -------------------------------------------------------------
    std::vector<FrameSlot> frameSlots_;
    u32 frameSlot_ = 0;
    u64 frameCounter_ = 0;
    bool frameBegun_ = false;
    bool workSubmitted_ = false;
    u32 drawCallCount_ = 0;

    std::string deviceName_;
    std::string driverVersion_;
    u32 maxTextureSize_ = 0;

    BackendInfo info_{};
    std::string lastError_;
};

/// Registry entry point. Keep the signature in sync with Registry.cpp.
IArgonBackend* CreateD3D12Backend();

} // namespace argon::ui
