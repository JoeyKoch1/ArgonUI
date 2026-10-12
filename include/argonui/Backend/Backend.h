// ArgonUI - Backend/Backend.h
//
// THE SWAPPABILITY LAYER.
//
// ArgonUI's core never includes a GPU SDK header and never links against one.
// It talks to exactly this interface. Swapping D3D11 for Vulkan, or rendering
// into a device someone else created, or hijacking an existing swapchain, is a
// configuration change - never a recompile of the UI.
//
// Three axes of ownership are deliberately separated, because they are the
// three things people actually need to change in an in-game overlay:
//
//   1. Device   - whose adapter/device/queue do we draw with?
//   2. Target   - whose swapchain / framebuffer / render target do we draw into?
//   3. Present  - who calls Present, and do we get called before or after?
//
// Typical overlay setup (D3D11 into a running game):
//     DeviceDesc d;  d.deviceSource = DeviceSource::External;
//                   d.externalDevice  = gameDevice;
//                   d.externalContext = gameImmediateContext;
//     TargetDesc  t;  t.targetSource = TargetSource::ExternalSwapchain;
//                   t.externalSwapchain = gameSwapchain;
//     backend->Initialize(d);
//     backend->AdoptTarget(t);
//     // in your hooked Present:
//     if (backend->BeginFrame(fs)) { backend->Render(drawList, fs); backend->Present(); }
//     backend->EndFrame();
//
// Typical standalone menu (own window, own everything):
//     DeviceDesc d; d.windowHandle = hwnd; d.width = 1280; d.height = 800;
//     TargetDesc  t; t.targetSource = TargetSource::OwnSwapchain;
#pragma once

#include <functional>

#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "../Core/Version.h"
#include "../Draw/DrawList.h"

namespace argon::ui {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

enum class GraphicsApi : u8 {
    None = 0,
    D3D11,
    D3D12,
    Vulkan,
    OpenGL,
    /// Records nothing; used for headless tests and for measuring CPU cost.
    Null,
};

enum class DeviceSource : u8 {
    /// The backend creates its own adapter, device and command queue.
    CreateInternal = 0,
    /// The host already has a device (the game's ID3D11Device, a VkDevice...).
    /// ArgonUI uses it but never destroys it.
    External,
};

enum class TargetSource : u8 {
    /// The backend creates and owns a swapchain for `windowHandle`.
    OwnSwapchain = 0,
    /// Use an existing swapchain we created earlier (we still own it).
    ExternalSwapchain,
    /// Draw into an offscreen texture. No present path at all.
    OffscreenTexture,
};

enum class PresentHook : u8 {
    /// We submit and call Present ourselves. Owner mode.
    Manual = 0,
    /// The host calls Render() from inside its own hooked Present.
    ManualBeforePresent,
    /// The host calls Render() from a hook that runs *after* Present.
    ManualAfterPresent,
};

enum class TextureFormat : u8 {
    R8 = 0,      ///< Single channel. ArgonUI's font atlas uses this when the
                ///< backend can sample .r as alpha (it can - we sample the
                ///< red channel into the shape's alpha).
    RGBA8,
    BGRA8,
};

// ---------------------------------------------------------------------------
// Descriptor structs
// ---------------------------------------------------------------------------

struct DeviceDesc {
    GraphicsApi api = GraphicsApi::None;

    DeviceSource deviceSource = DeviceSource::CreateInternal;

    /// External device handle. Type depends on `api`:
    ///   D3D11   -> ID3D11Device*
    ///   D3D12   -> ID3D12Device*   (and `externalCommandQueue` -> ID3D12CommandQueue*)
    ///   Vulkan  -> VkDevice (physical device taken from `externalInstance`)
    ///   OpenGL  -> nullptr (GL contexts are per-thread, nothing to share)
    void* externalDevice = nullptr;

    /// Immediate context / command queue / Vulkan command buffer.
    void* externalContext = nullptr;

    /// VkInstance when using External, or a surface-bearing instance otherwise.
    void* externalInstance = nullptr;
    /// VkSurfaceKHR for Vulkan (or nullptr to let the backend make one).
    void* externalSurface = nullptr;

    /// Native window: HWND, xcb_window_t, Wayland wl_surface*, NSWindow*.
    /// Required when creating our own swapchain; ignored otherwise.
    void* windowHandle = nullptr;

    u32 width = 0;
    u32 height = 0;

    bool enableVsync = true;
    /// Swapchain created with alpha so the overlay window composites.
    bool transparentBackground = false;
    /// MSAA sample count for the swapchain. 1 = off.
    u32 msaaSamples = 1;
    /// Debug layer / validation layers where available.
    bool enableValidation = false;
};

struct TargetDesc {
    TargetSource targetSource = TargetSource::OwnSwapchain;
    PresentHook presentHook = PresentHook::Manual;

    /// Existing swapchain to adopt:
    ///   D3D11/12 -> IDXGISwapChain*  (we do NOT take ownership)
    ///   Vulkan    -> VkSwapchainKHR
    ///   OpenGL    -> GLuint framebuffer name (0 = default framebuffer)
    void* externalSwapchain = nullptr;

    u32 width = 0;
    u32 height = 0;
    u32 msaaSamples = 1;

    /// If true, a hooked host should skip its own Present() on frames where
    /// ArgonUI submitted work. The backend signals this via Present().
    bool suppressHostPresent = false;
};

struct FrameState {
    u32 frameIndex = 0;
    f32 deltaTime = 0.0f;   ///< seconds, already clamped by the backend
    f32 time = 0.0f;        ///< seconds since start
    Vec2 displaySize{0.0f, 0.0f};
    f32 dpiScale = 1.0f;
    bool occluded = false;  ///< window minimised / on another space
    bool focused = true;
};

struct TextureDesc {
    u32 width = 0;
    u32 height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    bool filterLinear = true;
    bool repeat = false;
    bool srgb = false;
    /// ArgonUI's shader expects **straight** (non-premultiplied) alpha:
    ///     alpha = shapeMask * shapeAlpha * tex.a
    ///     rgb   = shapeColor * alpha * tex.rgb
    /// which is identical for glyph coverage, images and the white 1x1 used by
    /// shapes. Upload your pixel data verbatim; do not premultiply.
};

struct BackendInfo {
    GraphicsApi api = GraphicsApi::None;
    const char* deviceName = "";
    const char* driverVersion = "";
    const char* backendVersion = ARGONUI_VERSION;
    const char* shadingLanguage = "";
    u32 maxTextureSize = 0;
    u32 vertexBufferSize = 0;
    u32 indexBufferSize = 0;
    bool ownsDevice = false;
    bool ownsSwapchain = false;
    bool supportsPremultipliedBlending = true;
};

// ---------------------------------------------------------------------------
// The interface
// ---------------------------------------------------------------------------

class IArgonBackend {
public:
    virtual ~IArgonBackend() = default;

    // -- Setup -------------------------------------------------------------
    /// Create or adopt the device. Must be called before CreateTarget/AdoptTarget.
    virtual bool Initialize(const DeviceDesc& desc) = 0;
    virtual void Shutdown() = 0;

    // -- Target ------------------------------------------------------------
    /// Create our own swapchain / render target from a window.
    virtual bool CreateTarget(const TargetDesc& desc) = 0;

    /// Point the backend at a swapchain somebody else created. We render into
    /// it but never destroy it. Default: unsupported.
    virtual bool AdoptTarget(const TargetDesc& desc) {
        (void)desc;
        return false;
    }

    /// Resize. Backends must tolerate being called every frame with the same size.
    virtual bool ResizeTarget(u32 width, u32 height) = 0;

    virtual void DestroyTarget() = 0;
    virtual bool HasTarget() const = 0;
    virtual Vec2 TargetSize() const = 0;

    // -- Frame -------------------------------------------------------------
    /// Called once per frame before any drawing. Returns false when the frame
    /// should be skipped entirely (device removed, swapchain gone, occluded).
    virtual bool BeginFrame(const FrameState& state) = 0;

    /// Submit the frame's DrawList. Does not present.
    virtual void Render(const DrawList& list, const FrameState& state) = 0;

    /// Present, if we own the target. Returns false when we want the host to
    /// present instead (always the case for adopted targets).
    virtual bool Present() { return false; }

    /// Releases per-frame GPU allocations. Safe to call when BeginFrame failed.
    virtual void EndFrame() = 0;

    // -- Textures ----------------------------------------------------------
    /// Returns 0 on failure. `pixels` may be null to allocate uninitialised.
    virtual u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) = 0;
    virtual bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) = 0;
    virtual void DestroyTexture(u32 handle) = 0;

    // -- Introspection -----------------------------------------------------
    virtual const BackendInfo& Info() const = 0;
    virtual const char* Name() const = 0;

    /// Human-readable last error, empty when healthy.
    virtual const char* LastError() const = 0;
};

/// Signature used by the per-API factory functions.
using BackendCreateFn = std::function<IArgonBackend*()>;

} // namespace argon::ui