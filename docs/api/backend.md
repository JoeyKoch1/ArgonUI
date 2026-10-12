# Backend interface

The backend is the only part of ArgonUI that talks to a GPU. The core includes no
SDK header and links no SDK library; it hands a `DrawList` to an
`IArgonBackend` and expects pixels to appear. That boundary is why swapping
D3D11 for Vulkan, drawing into a device somebody else created, or driving an
existing swapchain is a configuration change rather than a recompile.

This page documents [`include/argonui/Backend/Backend.h`](../../include/argonui/Backend/Backend.h).
Read it if you are writing a backend, hooking an overlay into a game, or
debugging why nothing reaches the screen.

## The three ownership axes

The interface separates three things that people change independently when they
embed a UI into an existing application. Conflating any two of them is the usual
cause of a backend that crashes on shutdown or fights the host for the device.

**Device.** Whose adapter, device and command queue do we draw with? A
`DeviceDesc` answers this. Either the backend creates its own
(`DeviceSource::CreateInternal`) or it borrows one the host already has
(`DeviceSource::External`). When it borrows, ArgonUI uses the device but never
destroys it: the game that created the device also outlives it, and
`Shutdown()` must not tear it down.

**Target.** Whose swapchain, framebuffer or render target do we draw into? A
`TargetDesc` answers this. The backend can create and own a swapchain
(`TargetSource::OwnSwapchain`), render into a swapchain the application created
(`TargetSource::ExternalSwapchain`, still the application's to destroy), or draw
into an offscreen texture with no present path at all
(`TargetSource::OffscreenTexture`).

**Present.** Who calls present, and do we get invoked before or after the host?
A `PresentHook` answers this. In `Manual` mode the backend submits and presents
on its own. In `ManualBeforePresent` and `ManualAfterPresent` the host calls
`Render()` from inside a hook it installed on its own present chain, and the
backend deliberately does not present.

Each axis is orthogonal. The common overlay case borrows the game's device,
adopts the game's swapchain, and renders from a hook running before the game's
present: three axes, three borrowed resources, and `Shutdown()` destroys
nothing that the game created.

## Integration: a standalone window

The backend owns everything. Create a `DeviceDesc` naming the window and its
size, initialize, create the target, then drive the frame.

```cpp
DeviceDesc d;
d.api = GraphicsApi::D3D11;
d.windowHandle = hwnd;
d.width = 1280;
d.height = 800;

TargetDesc t;
t.targetSource = TargetSource::OwnSwapchain;
t.width = 1280;
t.height = 800;

backend->Initialize(d);
backend->CreateTarget(t);

// Per frame:
FrameState fs;
fs.frameIndex = frame;
fs.deltaTime = dt;
fs.displaySize = Vec2(1280.0f, 800.0f);
if (backend->BeginFrame(fs)) {
    backend->Render(drawList, fs);
    backend->Present();
}
backend->EndFrame();
```

## Integration: adopting a host device and swapchain

The host already has a device and a swapchain. ArgonUI borrows both and renders
from a hook the host installed.

```cpp
DeviceDesc d;
d.api = GraphicsApi::D3D11;
d.deviceSource = DeviceSource::External;
d.externalDevice = gameDevice;          // ID3D11Device*
d.externalContext = gameImmediateContext; // ID3D11DeviceContext*

TargetDesc t;
t.targetSource = TargetSource::ExternalSwapchain;
t.externalSwapchain = gameSwapchain;    // IDXGISwapChain*
t.presentHook = PresentHook::ManualBeforePresent;
t.width = 1920;
t.height = 1080;

backend->Initialize(d);
backend->AdoptTarget(t);

// Inside the hooked Present:
if (backend->BeginFrame(fs)) {
    backend->Render(drawList, fs);
    backend->Present();
}
backend->EndFrame();
```

{% hint style="warning" %}
`Present()` returns false for an adopted target by design. The host's own present
remains the one that reaches the screen. Do not treat that false as a failure.
{% endhint %}

## Enumerations

### `GraphicsApi`

The rendering API. `None` means unset.

| Member | Value | Meaning |
|---|---|---|
| `None` | 0 | No API selected. The default in `DeviceDesc`. |
| `D3D11` | 1 | Direct3D 11. |
| `D3D12` | 2 | Direct3D 12. |
| `Vulkan` | 3 | Vulkan. |
| `OpenGL` | 4 | OpenGL. |
| `Null` | 5 | Records nothing. For headless tests and for measuring CPU cost. |

### `DeviceSource`

Whose device we draw with.

| Member | Value | Meaning |
|---|---|---|
| `CreateInternal` | 0 | The backend creates its own adapter, device and command queue. |
| `External` | 1 | The host already has a device. ArgonUI uses it and never destroys it. |

### `TargetSource`

Whose swapchain or render target we draw into.

| Member | Value | Meaning |
|---|---|---|
| `OwnSwapchain` | 0 | The backend creates and owns a swapchain for `windowHandle`. |
| `ExternalSwapchain` | 1 | Use an existing swapchain we created earlier. We still own it. |
| `OffscreenTexture` | 2 | Draw into an offscreen texture. No present path at all. |

{% hint style="info" %}
`ExternalSwapchain` means the *application* created a swapchain earlier and
handed it over. The backend renders into it but never destroys it, the same
rule as an external device.
{% endhint %}

### `PresentHook`

Who calls present, and where we are invoked relative to the host's present.

| Member | Value | Meaning |
|---|---|---|
| `Manual` | 0 | We submit and call present ourselves. Owner mode. |
| `ManualBeforePresent` | 1 | The host calls `Render()` from inside its own hooked present. |
| `ManualAfterPresent` | 2 | The host calls `Render()` from a hook that runs after present. |

### `TextureFormat`

Pixel layout of a texture. ArgonUI's shader expects straight (non-premultiplied)
alpha in every case, so the pixel data you upload is used verbatim.

| Member | Value | Meaning |
|---|---|---|
| `R8` | 0 | Single channel. ArgonUI's font atlas uses this; the shader samples the red channel into the shape's alpha. |
| `RGBA8` | 1 | Four channels, red first in memory. |
| `BGRA8` | 2 | Four channels, blue first in memory. |

## Descriptor structs

Native handle types depend on `DeviceDesc::api`. The table gives the type per
API.

### `DeviceDesc`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `api` | `GraphicsApi` | `GraphicsApi::None` | Which API to create or bind against. |
| `deviceSource` | `DeviceSource` | `DeviceSource::CreateInternal` | Whether we make the device or borrow one. |
| `externalDevice` | `void*` | `nullptr` | Native device: D3D11 `ID3D11Device*`, D3D12 `ID3D12Device*`, Vulkan `VkDevice` (physical device from `externalInstance`). OpenGL needs none. |
| `externalContext` | `void*` | `nullptr` | Immediate context / command queue / Vulkan command buffer. D3D12 expects `ID3D12CommandQueue*`. |
| `externalInstance` | `void*` | `nullptr` | `VkInstance` when using External, or a surface-bearing instance otherwise. |
| `externalSurface` | `void*` | `nullptr` | `VkSurfaceKHR` for Vulkan, or nullptr to let the backend make one. |
| `windowHandle` | `void*` | `nullptr` | Native window: HWND, `xcb_window_t`, Wayland `wl_surface*`, `NSWindow*`. Required when creating our own swapchain, ignored otherwise. |
| `width` | `u32` | `0` | Requested width in pixels. |
| `height` | `u32` | `0` | Requested height in pixels. |
| `enableVsync` | `bool` | `true` | Request vsync on our own swapchain. |
| `transparentBackground` | `bool` | `false` | Create the swapchain with alpha so the overlay window composites. |
| `msaaSamples` | `u32` | `1` | MSAA sample count. 1 means off. |
| `enableValidation` | `bool` | `false` | Enable the debug / validation layers where available. |

### `TargetDesc`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `targetSource` | `TargetSource` | `TargetSource::OwnSwapchain` | Whose swapchain or render target to draw into. |
| `presentHook` | `PresentHook` | `PresentHook::Manual` | Where we are invoked relative to the host's present. |
| `externalSwapchain` | `void*` | `nullptr` | Existing swapchain to adopt. D3D11/12 `IDXGISwapChain*` (not owned), Vulkan `VkSwapchainKHR`, OpenGL a `GLuint` framebuffer name (0 = default framebuffer). |
| `width` | `u32` | `0` | Target width in pixels. |
| `height` | `u32` | `0` | Target height in pixels. |
| `msaaSamples` | `u32` | `1` | MSAA sample count for the target. |
| `suppressHostPresent` | `bool` | `false` | When true, a hooked host should skip its own present on frames where ArgonUI submitted work. The backend signals such a frame via `Present()`. |

### `FrameState`

Per-frame inputs the backend needs and the application supplies.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `frameIndex` | `u32` | `0` | Monotonic frame counter. |
| `deltaTime` | `f32` | `0.0f` | Seconds since the previous frame, already clamped by the backend. |
| `time` | `f32` | `0.0f` | Seconds since start. |
| `displaySize` | `Vec2` | `{0.0f, 0.0f}` | Target size in pixels. |
| `dpiScale` | `f32` | `1.0f` | Device pixel ratio. |
| `occluded` | `bool` | `false` | Window minimised or on another space. |
| `focused` | `bool` | `true` | Window has input focus. |

### `TextureDesc`

| Field | Type | Default | Meaning |
|---|---|---|---|
| `width` | `u32` | `0` | Width in pixels. |
| `height` | `u32` | `0` | Height in pixels. |
| `format` | `TextureFormat` | `TextureFormat::RGBA8` | Pixel layout. |
| `filterLinear` | `bool` | `true` | Sample with a linear filter. |
| `repeat` | `bool` | `false` | Wrap mode is repeat rather than clamp. |
| `srgb` | `bool` | `false` | Sample through an sRGB decode. |

ArgonUI's shader expects **straight** (non-premultiplied) alpha:

```
alpha = shapeMask * shapeAlpha * tex.a
rgb   = shapeColor * alpha * tex.rgb
```

That single formula is identical for glyph coverage, for images and for the
flat white 1x1 used by solid shapes. Upload your pixel data verbatim; do not
premultiply.

{% hint style="warning" %}
Premultiplying before upload is the single most common backend bug. It squares
the alpha on every glyph and image edge, so thin strokes and soft glows come out
too faint and too dark. Pass the bytes exactly as you produced them.
{% endhint %}

### `BackendInfo`

What the backend reports about itself.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `api` | `GraphicsApi` | `GraphicsApi::None` | The API in use. |
| `deviceName` | `const char*` | `""` | Adapter name. |
| `driverVersion` | `const char*` | `""` | Driver version string. |
| `backendVersion` | `const char*` | `ARGONUI_VERSION` | ArgonUI version the backend was built against. |
| `shadingLanguage` | `const char*` | `""` | Shader language, for example `"HLSL"`, `"GLSL"`, `"SPIR-V"`. |
| `maxTextureSize` | `u32` | `0` | Largest texture dimension the device supports. |
| `vertexBufferSize` | `u32` | `0` | Vertex buffer capacity in bytes. |
| `indexBufferSize` | `u32` | `0` | Index buffer capacity in bytes. |
| `ownsDevice` | `bool` | `false` | True when the backend created and must destroy the device. |
| `ownsSwapchain` | `bool` | `false` | True when the backend created and must destroy the swapchain. |
| `supportsPremultipliedBlending` | `bool` | `true` | Whether the blend state can be switched to premultiplied. |

### `BackendCreateFn`

```cpp
using BackendCreateFn = std::function<IArgonBackend*()>;
```

The signature the per-API factory functions use. See
[Backend registry](registry.md) for how backends get discovered and created.

## The interface

`IArgonBackend` is an abstract base. You implement every pure-virtual method;
`AdoptTarget` and `Present` have default bodies for backends that do not support
them.

### `IArgonBackend::Initialize`

```cpp
virtual bool Initialize(const DeviceDesc& desc) = 0;
```

Creates the device or binds to an existing one. Must be called before
`CreateTarget` or `AdoptTarget`.

**Returns:** true on success. On failure `LastError()` explains why.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `desc` | `const DeviceDesc&` | | Device configuration. |

What a backend gets wrong here: allocating the swapchain or a render target
inside `Initialize`. Target setup belongs in `CreateTarget`/`AdoptTarget`, because
a host may hand over a swapchain later, after the device already exists.

### `IArgonBackend::Shutdown`

```cpp
virtual void Shutdown() = 0;
```

Releases everything the backend owns. Must not destroy an external device or an
adopted swapchain.

**Returns:** nothing.

What a backend gets wrong here: destroying the device in `External` mode, or
releasing textures while the host may still be mid-present. Order matters.
Release the target before the device.

### `IArgonBackend::CreateTarget`

```cpp
virtual bool CreateTarget(const TargetDesc& desc) = 0;
```

Creates our own swapchain or render target, normally from `windowHandle`.

**Returns:** true on success.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `desc` | `const TargetDesc&` | | Target configuration. |

What a backend gets wrong here: creating a swapchain sized from a stale
`width`/`height` instead of querying the live window size. On a resized window
the swapchain must be rebuilt from the current client area.

### `IArgonBackend::AdoptTarget`

```cpp
virtual bool AdoptTarget(const TargetDesc& desc) {
    (void)desc;
    return false;
}
```

Points the backend at a swapchain somebody else created. We render into it but
never destroy it. The default body returns false, so a backend that cannot
adopt does not have to implement it.

**Returns:** true if the swapchain was adopted. False by default, and false when
the swapchain cannot be bound.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `desc` | `const TargetDesc&` | | Target configuration, including `externalSwapchain`. |

What a backend gets wrong here: taking ownership of the adopted swapchain and
releasing it in `Shutdown()`. That corrupts the host's frame.

### `IArgonBackend::ResizeTarget`

```cpp
virtual bool ResizeTarget(u32 width, u32 height) = 0;
```

Resizes the target. Backends must tolerate being called every frame with the
same size, so gate the actual swapchain rebuild on a size change.

**Returns:** true on success.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `width` | `u32` | | New width in pixels. |
| `height` | `u32` | | New height in pixels. |

What a backend gets wrong here: rebuilding the swapchain unconditionally, which
costs a synchronous GPU flush every frame. Compare against the current size
first.

### `IArgonBackend::DestroyTarget`

```cpp
virtual void DestroyTarget() = 0;
```

Releases the render target. Safe to call when there is no target.

**Returns:** nothing.

What a backend gets wrong here: destroying an adopted swapchain. Only destroy
what `CreateTarget` made, or what `ownsSwapchain` reports true.

### `IArgonBackend::HasTarget`

```cpp
virtual bool HasTarget() const = 0;
```

Reports whether a usable target currently exists.

**Returns:** true if a target is bound.

What a backend gets wrong here: returning true after the host destroyed the
swapchain out from under us. Re-check liveness at `BeginFrame`, not just at
adoption.

### `IArgonBackend::TargetSize`

```cpp
virtual Vec2 TargetSize() const = 0;
```

Current target size in pixels.

**Returns:** the target size, or `Vec2(0, 0)` when there is no target.

What a backend gets wrong here: returning the device size instead of the target
size. For an adopted swapchain those differ.

### `IArgonBackend::BeginFrame`

```cpp
virtual bool BeginFrame(const FrameState& state) = 0;
```

Called once per frame before any drawing. Returns false when the frame should be
skipped entirely (device removed, swapchain gone, occluded).

**Returns:** true to proceed with `Render`/`Present`, false to skip the frame.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `state` | `const FrameState&` | | Per-frame timing, size and focus state. |

What a backend gets wrong here: returning true on an occluded or device-lost
frame and then crashing in `Render`. The false path is the contract for
"nothing to do this frame". Still call `EndFrame` after a false.

### `IArgonBackend::Render`

```cpp
virtual void Render(const DrawList& list, const FrameState& state) = 0;
```

Submits the frame's draw list. Does not present.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `list` | `const DrawList&` | | The draw list produced this frame. Owned by the Context. |
| `state` | `const FrameState&` | | The same state passed to `BeginFrame`. |

What a backend gets wrong here: calling present, or reading `list` after it goes
out of scope. The list is owned by the `Context` and is only valid for the call.

### `IArgonBackend::Present`

```cpp
virtual bool Present() { return false; }
```

Presents, if we own the target. Returns false when we want the host to present
instead, which is always the case for adopted targets.

**Returns:** true if the backend presented. False if the host should present.

What a backend gets wrong here: presenting when it only adopted the target, or
presenting on a frame where `BeginFrame` returned false.

### `IArgonBackend::EndFrame`

```cpp
virtual void EndFrame() = 0;
```

Releases per-frame GPU allocations. Safe to call when `BeginFrame` failed.

**Returns:** nothing.

What a backend gets wrong here: assuming a successful `BeginFrame`. `EndFrame`
runs on the skip path too, so per-frame resets there must not touch a target
that was never opened.

### `IArgonBackend::CreateTexture`

```cpp
virtual u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) = 0;
```

Allocates a texture and optionally uploads initial pixels. The default argument
allocates uninitialized memory.

**Returns:** a nonzero texture handle, or 0 on failure.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `desc` | `const TextureDesc&` | | Size, format and sampler state. |
| `pixels` | `const void*` | `nullptr` | Initial pixel data, uploaded verbatim. May be null to allocate uninitialized. |

What a backend gets wrong here: premultiplying or converting the pixel data. The
shader consumes straight alpha. Also return 0, not a valid handle, on failure,
because callers test for 0.

### `IArgonBackend::UpdateTexture`

```cpp
virtual bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) = 0;
```

Replaces a texture's contents.

**Returns:** true on success.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `handle` | `u32` | | Texture returned by `CreateTexture`. |
| `desc` | `const TextureDesc&` | | New size, format and sampler state. |
| `pixels` | `const void*` | | New pixel data, uploaded verbatim. |

What a backend gets wrong here: uploading the bytes of the old format after the
descriptor changed, or skipping the row pitch. The dimensions in `desc` govern
the stride.

### `IArgonBackend::DestroyTexture`

```cpp
virtual void DestroyTexture(u32 handle) = 0;
```

Releases a texture. Safe to call with 0 or an already-destroyed handle.

**Returns:** nothing.

What a backend gets wrong here: tearing down a texture while it is still bound
to an in-flight draw call. Retire it at `EndFrame`, not mid-frame.

### `IArgonBackend::Info`

```cpp
virtual const BackendInfo& Info() const = 0;
```

Introspection about the device and the backend build.

**Returns:** a const reference to the backend's `BackendInfo`.

What a backend gets wrong here: returning a reference to a temporary. The
`BackendInfo` must outlive the call.

### `IArgonBackend::Name`

```cpp
virtual const char* Name() const = 0;
```

Short backend name, for logs and diagnostics.

**Returns:** a null-terminated name such as `"D3D11"`.

### `IArgonBackend::LastError`

```cpp
virtual const char* LastError() const = 0;
```

Human-readable description of the most recent failure.

**Returns:** a message, or an empty string when healthy.

What a backend gets wrong here: returning a stale error after a successful call.
Clear it on the next successful operation.

## See also

- [Backend registry](registry.md) - picking and creating a backend at runtime.
- [Backend ownership](../concepts/backend-ownership.md) - the three axes in prose.
- [Overlay guide](../guides/overlay.md) - rendering into a game's device.
- [Vertex format](vertex.md) - the vertex data a backend consumes.