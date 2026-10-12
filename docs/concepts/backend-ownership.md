# Backend ownership

This is the page to read before writing any integration code. Almost every hard
problem in using a UI library inside a game comes down to who owns what.

## Three axes

ArgonUI separates three things that people usually have to change together:

| Axis | Question | Options |
|---|---|---|
| **Device** | Whose adapter, device and queue do we draw with? | `CreateInternal`, `External` |
| **Target** | Whose swapchain or framebuffer do we draw into? | `OwnSwapchain`, `ExternalSwapchain`, `OffscreenTexture` |
| **Present** | Who calls `Present`, and do we run before or after it? | `Manual`, `ManualBeforePresent`, `ManualAfterPresent` |

Most setups pick one value on each axis and never touch them again. The point of
splitting them is that you can.

## Device source

### `CreateInternal`

ArgonUI creates the adapter, device and queue. On Windows it tries hardware
first and falls back to WARP, so a headless machine or an RDP session still
renders. You must destroy it yourself, or let `Context::Shutdown` do it.

Use this for a standalone menu window, a design tool, or a test harness.

### `External`

You hand ArgonUI a device and an immediate context that somebody else created.
ArgonUI uses them and never releases them.

```cpp
DeviceDesc d;
d.api            = GraphicsApi::D3D11;
d.deviceSource   = DeviceSource::External;
d.externalDevice  = gameDevice;             // ID3D11Device*
d.externalContext = gameImmediateContext;    // ID3D11DeviceContext*
```

Two behaviours follow, and both matter:

**Context state is saved and restored.** With `External`, the D3D11 backend
wraps its frame in `ID3D11DeviceContext::GetState` and `SetState`, so every
shader, buffer, rasterizer and blend state the host set is back in place
afterwards. It costs about a megabyte of copies per frame. It is worth it,
because the alternative is a game that renders wrong only on the frames your
overlay was open.

**D3D12 waits on the previous fence as well as the current one.** Upload memory
is persistently mapped and shared across frames, so waiting only on the current
frame's fence would let frame N overwrite data frame N-2's copy is still
reading. One frame of overlap, correctness guaranteed.

## Target source

### `OwnSwapchain`

ArgonUI creates a swapchain for `DeviceDesc::windowHandle`. It owns it and
destroys it. This is the standalone case.

### `ExternalSwapchain`

You hand ArgonUI a swapchain somebody else created. It renders into it and
never destroys it.

```cpp
TargetDesc t;
t.targetSource     = TargetSource::ExternalSwapchain;
t.externalSwapchain = gameSwapChain;   // IDXGISwapChain*
backend->AdoptTarget(t);
```

This is the in-game case. `AdoptTarget` refuses if a target is already
attached, so call `DestroyTarget` first if you are swapping a game.

### `OffscreenTexture`

ArgonUI creates a colour texture and renders into it with no present path at
all. Useful for capturing a frame of the menu, or for rendering on a worker
thread and compositing later.

## Present

`Present()` on the backend returns:

- `true` when it owns the swapchain and called `Present`
- `false` when the target was adopted and the host presents

So `Context::Render` can submit unconditionally and the right thing happens:

```cpp
void Context::Render() {
    backend_->BeginFrame(frame_);
    backend_->Render(drawList_, frame_);
    backend_->Present();      // false for adopted targets
    backend_->EndFrame();
}
```

### Running inside someone else's Present

This is the pattern for an overlay. Hook the game's `Present`, and before
calling through, run one ArgonUI frame:

```cpp
// inside your Present hook
if (overlayVisible) {
    argon::ui::FrameState fs;
    fs.displaySize = { (float)backBufferWidth, (float)backBufferHeight };
    fs.deltaTime   = dt;

    overlayCtx.NewFrame(fs);
    overlayCtx.BeginLayout(Rect(0, 0, backBufferWidth, backBufferHeight));
    DrawOverlayMenu(overlayCtx);
    overlayCtx.EndLayout();
    overlayCtx.Render();          // submits, Present() returns false
}
return originalPresent(pSwapChain, syncInterval, flags);
```

Two details matter here.

**Resize.** If the game's swapchain is resized, ArgonUI's render target is now
stale. Call `ResizeTarget` when the size changes. If you miss it, the symptom is
a black overlay after a window resize, and the D3D11 backend logs the
`DXGI_ERROR_INVALIDCALL` reason.

**Order.** Do not draw before the game's own scene if you want the menu on top.
`PresentAfter` means "after their draw calls, before their Present", which is the
usual position.

## Choosing a target format

`DeviceDesc::transparentBackground` sets the swapchain alpha mode. With it on,
ArgonUI creates the chain with `DXGI_ALPHA_MODE_PREMULTIPLIED`, which is what a
layered window needs.

The output is premultiplied alpha throughout, because that is what the shader
produces and what makes glyph coverage, images and flat fills share one blend
equation.

## Backend selection at runtime

```cpp
GraphicsApi api = SelectBackend(GraphicsApi::None);
// honours ARGONUI_BACKEND, else picks the first compiled-in API

IArgonBackend* backend = CreateBackend(api);
```

`CreateBackend` returns null for anything not compiled in and logs a warning.
`IsBackendAvailable(api)` asks the question without constructing anything.

## Writing your own backend

Implement `IArgonBackend` and it drops into the registry. The contract the
existing four satisfy:

- `Present()` returns `false` for adopted targets.
- `BeginFrame()` returns `false` when there is no device, no render target, the
  frame is already open, or the frame is occluded.
- `LastError()` is non-empty only when something is wrong.
- Adopted objects are never released in `Shutdown`.
- Output is premultiplied alpha with blend `(ONE, ONE_MINUS_SRC_ALPHA)`.
- Shader input is the 64-byte vertex in [Vertex format](vertex-format.md).

See [Choosing a backend](../guides/choosing-a-backend.md) for whether you need
to.

## See also

- [Backend interface reference](../api/backend.md)
- [Backend registry reference](../api/registry.md)
- [Rendering into someone else's device](../guides/overlay.md)