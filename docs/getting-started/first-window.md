# Your first window

A complete application: create a window, give ArgonUI a device and a target,
then draw a frame. Win32 and Direct3D 11, because that is the combination the
demo uses.

## What ArgonUI does not do

ArgonUI does not open a window, create a device, or pump events. It has no
platform layer at all. Three things are yours:

1. A window and an event loop
2. Filling an `InputState` each frame from your events
3. Deciding who owns the device and the swapchain

This is deliberate. A menu that draws over a running game cannot own any of
those things, and an abstraction that always owns them is the wrong
abstraction. See [Backend ownership](../concepts/backend-ownership.md).

## The application

```cpp
#include <windows.h>
#include "argonui/ArgonUI.h"
#include "argonui/Backend/Registry.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // ---- window -------------------------------------------------------
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"App";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"App", L"ArgonUI",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    // ---- backend ------------------------------------------------------
    // SelectBackend resolves an API enum, honouring ARGONUI_BACKEND.
    const argon::ui::GraphicsApi api =
        argon::ui::SelectBackend(argon::ui::GraphicsApi::D3D11);
    argon::ui::IArgonBackend* backend = argon::ui::CreateBackend(api);
    if (backend == nullptr) return 1;   // nothing compiled in for that api

    argon::ui::DeviceDesc device;
    device.api           = api;
    device.deviceSource  = argon::ui::DeviceSource::CreateInternal;
    device.windowHandle  = hwnd;
    device.width         = 1280;
    device.height        = 800;
    device.enableVsync   = true;
    device.msaaSamples   = 1;   // the shader anti-aliases; MSAA double-filters
    backend->Initialize(device);

    argon::ui::TargetDesc target;
    target.targetSource = argon::ui::TargetSource::OwnSwapchain;
    target.width  = 1280;
    target.height = 800;
    backend->CreateTarget(target);

    // ---- context ------------------------------------------------------
    argon::ui::Context ctx;
    argon::ui::ContextConfig config;
    config.dpiScale = 1.0f;
    ctx.Initialize(backend, config);

    argon::ui::InputState io;
    bool running = true;
    float dt = 0.0f;

    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        io.ClearAll();
        io.mousePos = GetCursorPosInClientSpace(hwnd);
        io.mouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        argon::ui::FrameState fs;
        fs.displaySize = { (float)clientW, (float)clientH };
        fs.deltaTime   = dt;
        fs.time       += dt;
        fs.dpiScale    = windowDpiScale(hwnd);

        ctx.NewFrame(fs);
        ctx.BeginLayout(argon::ui::Rect(0, 0, clientW, clientH));

        if (argon::ui::BeginWindow(ctx, "main", "Hello", {720, 480})) {
            bool open = true;
            argon::ui::BeginSection(ctx, "Settings", &open);
                argon::ui::RowToggle(ctx, "Enabled", &enabled);
                argon::ui::RowSlider(ctx, "Volume", &volume, 0.0f, 1.0f, "%.2f");
            argon::ui::EndSection(ctx);

            argon::ui::Button("Click me", argon::ui::ButtonKind::Primary);
        }

        argon::ui::RenderTooltips(ctx);   // always last
        ctx.EndLayout();
        ctx.Render();
    }

    ctx.Shutdown();
    return 0;
}
```

## What each frame does

| Call | Purpose |
|---|---|
| `ctx.NewFrame(fs)` | Resets the draw list, derives input edges, advances animations. |
| `ctx.BeginLayout(rect)` | Sets the region widgets stack inside. |
| widgets | Append primitives to the draw list. |
| `RenderTooltips(ctx)` | Draws queued tooltips above everything. |
| `ctx.EndLayout()` | Pops the layout. |
| `ctx.Render()` | Submits the draw list and presents. |

Order matters in exactly one place: `RenderTooltips` after all other widgets,
because it draws on top. Everything else is order independent.

## Getting the window size right

`FrameState::displaySize` and `InputState::mousePos` must be in the same space.
With a Direct3D 11 swapchain that is client pixels, so a mouse position from
`GET_X_LPARAM(lParam)` needs no conversion. If you render into a texture at a
different resolution than your window, scale the mouse position to match or
every hit test will be off.

Call `ResizeTarget` whenever the size changes. Calling it every frame with an
unchanged size is safe, the backend ignores it.

## Common mistakes

**Calling `RenderTooltips` early.** Tooltips queue during the frame and draw at
`RenderTooltips`. Call it last.

**Forgetting `EndLayout`.** Every `BeginLayout` needs one. A mismatched pair
leaves the cursor somewhere unexpected for the rest of the frame.

**Setting `mouseClicked` yourself.** `NewFrame` derives every edge flag from the
`mouseDown` latches you fill in. Writing `mouseClicked` as well fights it. See
[Input](../api/input.md).

**Leaving MSAA on.** The shader anti-aliases analytically. Hardware MSAA on top
double-filters and softens every edge.

## Next

- [Driving the UI from a platform loop](platform-loop.md) for the input contract
- [Containers, pickers and overlays](../api/widgets.md) for the widget reference
- [Rendering into someone else's device](../guides/overlay.md) when you are past
  a standalone window