# Rendering into someone else's device

This is the case ArgonUI was built around: a menu that draws over a running game,
using the device and the swapchain the game already created.

## The shape of it

```cpp
// once, after the game is up
argon::ui::IArgonBackend* backend =
    argon::ui::CreateBackend(argon::ui::GraphicsApi::D3D11);

argon::ui::DeviceDesc device;
device.api            = argon::ui::GraphicsApi::D3D11;
device.deviceSource   = argon::ui::DeviceSource::External;
device.externalDevice  = gameDevice;              // ID3D11Device*
device.externalContext = gameImmediateContext;     // ID3D11DeviceContext*
backend->Initialize(device);

argon::ui::TargetDesc target;
target.targetSource     = argon::ui::TargetSource::ExternalSwapchain;
target.externalSwapchain = gameSwapChain;         // IDXGISwapChain*
backend->AdoptTarget(target);

argon::ui::Context ctx;
ctx.Initialize(backend);
```

Then in your `Present` hook:

```cpp
HRESULT PresentHook(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (overlayVisible) {
        argon::ui::FrameState fs;
        fs.displaySize = { (float)backBufferWidth, (float)backBufferHeight };
        fs.deltaTime   = dt;

        ctx.NewFrame(fs);
        ctx.BeginLayout(argon::ui::Rect(0, 0, backBufferWidth, backBufferHeight));

        DrawOverlayMenu(ctx);          // your widget tree

        ctx.Render();                  // submits; Present() returns false
    }
    return originalPresent(sc, sync, flags);
}
```

`ctx.Render` calls `backend->Present()`, which returns `false` because the
swapchain was adopted. Your hook then calls through to the game's own `Present`
and the frame appears normally with your menu composited on top.

## Getting the swapchain

ArgonUI adopts a swapchain you give it. Getting the game's is the host's job:

```cpp
// from an ID3D11DeviceContext, walk to the swap chain via IDXGIDevice
IDXGIDevice*  dxgiDevice = nullptr;
IDXGIAdapter* adapter    = nullptr;
IDXGIFactory* factory    = nullptr;

gameContext->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
dxgiDevice->GetAdapter(&adapter);
adapter->GetParent(IID_PPV_ARGS(&factory));

// or, if you already hooked it, you have it
IDXGISwapChain* gameSwapChain = hookedSwapChain;
```

Many hosts also have an anti-cheat or a mod loader that hands you the swapchain
directly. Use whichever you have; ArgonUI does not care how you obtained it.

## Draw order

Where you draw relative to the game's own passes decides whether your menu is
under or over them.

| Position | Result |
|---|---|
| Before the game's scene | Your menu gets drawn over by the game. |
| After the game's scene, before `Present` | Menu on top. This is what you want. |
| Inside `EndDraw` or similar | Depends on the engine. |

ArgonUI does not sort or depth-test, it draws in submission order, so position
in the frame is the only control you have.

## Resize

This is the one that produces a black overlay after a window resize.

```cpp
if (lastWidth != backBufferWidth || lastHeight != backBufferHeight) {
    lastWidth  = backBufferWidth;
    lastHeight = backBufferHeight;
    backend->ResizeTarget((u32)backBufferWidth, (u32)backBufferHeight);
}
```

`ResizeTarget` releases the render target view and the back buffer before
calling `ResizeBuffers`. Missing either is the classic `DXGI_ERROR_INVALIDCALL`,
and the D3D11 backend logs the reason when it happens.

Call it in the same hook, before drawing. Calling it every frame with an
unchanged size is harmless.

## Device loss

A `Present` hook runs while the device can be lost. `BeginFrame` returns false
when there is no render target, so ArgonUI already degrades quietly. Handle the
larger case yourself:

```cpp
if (deviceRemoved) {
    overlayCtx.Shutdown();
    backend->Shutdown();
    delete backend;
    backend = nullptr;      // reinitialise once a new device exists
}
```

## State hygiene

With `DeviceSource::External`, the D3D11 backend saves and restores the full
device context state around its frame, so the host's next frame is unaffected.
That costs a couple of megabytes of copies per frame, which is cheap next to
debugging a game that renders wrong only while your overlay is open.

If you write your own backend and adopt a host device, do the same.

## Multi-monitor and DPI

Report the real DPI so text bakes at the right size:

```cpp
fs.dpiScale = (float)GetDpiForWindow(hwnd) / 96.0f;
```

When the overlay spans monitors with different densities, pick one and stay
consistent, or text will re-bake as the pointer crosses the boundary.

## Layered window overlays

To put the menu in its own transparent window rather than compositing into the
game's back buffer, set `DeviceDesc::transparentBackground` and create the
swapchain with `DXGI_ALPHA_MODE_PREMULTIPLIED`. ArgonUI's output is premultiplied
throughout, which is what a layered window expects.

This is a different integration from the one above: you own the window, so
`TargetSource::OwnSwapchain` applies and `Present()` returns true. You do not
need a hook at all.

## See also

- [Backend ownership](../concepts/backend-ownership.md)
- [Choosing a backend](choosing-a-backend.md)
- [Context reference](../api/context.md)