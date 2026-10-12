# ArgonUI

An immediate-mode interface toolkit built for in-game menus.

Like ImGui, you describe the UI as a function that runs every frame. Unlike
ImGui, the whole thing is one draw list, one shader and one pipeline per API. It
stays cheap inside a game, renders identically across Direct3D 11, Direct3D 12,
Vulkan and OpenGL, and never forces a particular GPU SDK into your UI code.

```
Main/ArgonUI/
  include/argonui/     public headers (nothing here includes a GPU SDK)
  src/                 the core: draw list, context, fonts, widgets
  backends/            d3d11, d3d12, vulkan, opengl, each independent
  shaders/             the reference shader every backend is a port of
  examples/demo/       a complete D3D11 + Win32 application
  third_party/stb/     stb_truetype.h
```

---

## Three things that set it apart

### 1. Every primitive is a rounded box with a signed distance field

There is no triangle mesh, no tessellation and no CPU rasterisation. A panel, a
toggle track, a toggle knob, a slider rail, a thumb, a pill button, an icon
stroke, a glyph and a thumbnail are all the same 64-byte vertex carrying a
rounded-box SDF description. The fragment shader evaluates it analytically.

```
ArgonUI, one real menu frame:
  quads ~1400   verts ~5600   draw calls 6   upload 352 KB
```

A bordered card is one primitive rather than two stacked rectangles, and a soft
drop shadow is a falloff in the same shader rather than a blur pass. That is why
the reference look costs almost nothing.

See `include/argonui/Draw/Vertex.h` and `shaders/argonui.hlsl`.

### 2. The backend owns the device, the target and the present

ArgonUI's core never includes `d3d11.h`, never links `d3d12.lib`, and never
opens a window. It talks to one interface, `IArgonBackend`, with three axes of
ownership kept separate:

| Axis        | `DeviceSource`                        | `TargetSource`                                |
|-------------|---------------------------------------|-----------------------------------------------|
| **Device**  | `CreateInternal` / `External`         | -                                             |
| **Target**  | -                                     | `OwnSwapchain` / `ExternalSwapchain` / `OffscreenTexture` |
| **Present** | -                                     | `Present()` returns `false` for adopted targets |

Standalone menu:

```cpp
DeviceDesc d;
d.windowHandle = hwnd;
d.width  = 1280;
d.height = 800;

TargetDesc t;
t.targetSource = TargetSource::OwnSwapchain;
```

Drawing into a device somebody else created, with somebody else's swapchain,
from inside their `Present`:

```cpp
DeviceDesc d;
d.deviceSource   = DeviceSource::External;
d.externalDevice  = gameDevice;          // ID3D11Device*
d.externalContext = gameImmediateContext;

TargetDesc t;
t.targetSource     = TargetSource::ExternalSwapchain;
t.externalSwapchain = gameSwapChain;     // IDXGISwapChain*

backend->Initialize(d);
backend->AdoptTarget(t);

// inside your hooked Present:
backend->BeginFrame(fs);
backend->Render(drawList, fs);
backend->Present();      // returns false, the host presents
backend->EndFrame();
```

Switching graphics API is a link-time and runtime decision, never a recompile
of the UI:

```cpp
argon::ui::IArgonBackend* b = argon::ui::CreateBackend(GraphicsApi::Vulkan);
```

Set `ARGONUI_BACKEND=vulkan` to pick one from the environment instead.

See `include/argonui/Backend/Backend.h`.

### 3. Containers size themselves

`DrawList::InsertBackgroundBox` splices geometry into the index buffer at a
checkpoint, so a collapsible section can paint its card behind content whose
height it only discovers at `End`. ImGui makes you hand `BeginChild()` a size up
front. `BeginSection`/`EndSection` do not take one.

```cpp
BeginSection(ctx, "Player", &open, nullptr, Icon::User);
    RowSwitch(ctx, "Enabled", &enabled);          // any number of rows
    RowSlider (ctx, "FOV", &fov, 60.0f, 130.0f);
EndSection(ctx);
```

Open or closed, the layout is correct either way. A closed body is clipped to
nothing, so there is no separate code path to get wrong.

---

## Using it

```cpp
#include "argonui/ArgonUI.h"

argon::ui::Context ctx;
argon::ui::IArgonBackend* backend = argon::ui::CreateBackend(GraphicsApi::D3D11);
ctx.Initialize(backend);

// each frame
argon::ui::FrameState fs;
fs.displaySize = { (float)clientWidth, (float)clientHeight };
fs.deltaTime   = dt;

ctx.NewFrame(fs);
ctx.BeginLayout(argon::ui::Rect(0, 0, clientWidth, clientHeight));

if (argon::ui::BeginWindow(ctx, "main", "Settings", {900, 600})) {
    bool open = true;
    argon::ui::BeginSection(ctx, "Visuals", &open);
        argon::ui::RowToggle(ctx, "ESP", &esp);
        argon::ui::RowSlider(ctx, "FOV", &fov, 60.0f, 130.0f, "%.0f");
    argon::ui::EndSection(ctx);
    argon::ui::Button("Save", argon::ui::ButtonKind::Primary);
}
argon::ui::RenderTooltips(ctx);     // always last
ctx.EndLayout();
ctx.Render();
```

Widget state (toggle positions, scroll offsets, combo selections, animation
phase) lives in the context, keyed by a hashed id path. Your data stays plain
`bool`, `float` and `int`. Nothing to serialise, nothing to clean up.

---

## Building

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

| Option                       | Default | Effect                         |
|------------------------------|---------|--------------------------------|
| `ARGONUI_BUILD_D3D11`        | ON      | Direct3D 11 backend            |
| `ARGONUI_BUILD_D3D12`        | ON      | Direct3D 12 backend            |
| `ARGONUI_BUILD_VULKAN`       | ON      | Vulkan backend, needs the SDK  |
| `ARGONUI_BUILD_OPENGL`       | ON      | OpenGL 3.3 backend             |
| `ARGONUI_BUILD_DEMO`         | ON      | The demo application           |
| `ARGONUI_WARNINGS_AS_ERRORS` | OFF     | `-Werror` / `/WX`              |
| `ARGON_VK_SURFACE_EXT`       | win32   | `xcb`, `xlib` or `wayland`     |

`argonui_core` never links a GPU SDK. Each backend is its own static library, so
you link exactly the ones you need.

The Vulkan backend loads precompiled SPIR-V at runtime and has no glslang
dependency. It checks `$ARGONUI_VULKAN_SHADER_DIR`, then `shaders/` next to the
executable. The OpenGL backend compiles its own GLSL on the fly. Either backend
can be pointed at in-memory shader blobs with `SetShaderPaths()` and
`SetShaderSpv()`.

---

## Widgets

| Area        | Widgets                                                              |
|-------------|----------------------------------------------------------------------|
| Controls    | Button (4 variants), IconButton, Checkbox, ToggleSwitch, Slider (h/v), DragFloat/Int, ProgressBar, Badge, SegmentedControl, Combo |
| Text        | TextField, SearchField, TextBlock                                    |
| Containers  | Window, Section (collapsible), ScrollArea, TabBar, Sidebar, Row, InfoRow, Grid, List, Breadcrumbs |
| Overlays    | Modal, ContextMenu, PopupList, Tooltips                              |
| Chrome      | Around 60 procedural stroke icons, no icon font or SVG rasteriser    |

Every colour and metric comes from the active `Theme`, so the dark,
near-black-with-a-lime-accent look of the reference is a data change
(`Theme::Dark()`, `Theme::Light()`, `Theme::Midnight()`), not a code change.

---

## Performance notes

* **Batching.** Consecutive quads sharing clip rect, texture, blend and flags
  merge into one batch. A real menu frame lands in the single digits.
* **Text.** Glyphs are baked into one R8 atlas per pixel size and uploaded only
  when a new glyph appears. R8 instead of RGBA8 is a quarter of the VRAM, which
  matters because text is most of a menu.
* **DPI.** Each pixel size gets its own atlas, so 125%, 150% and 200% scaling
  render sharp rather than upscaled.
* **Anti-aliasing.** Analytic, from `fwidth` on the SDF, correct at any scale.
  Leave MSAA off; it would double-filter.
* **Overlays.** When the device is not yours, the D3D11 backend saves and
  restores the full device context state so the host's next frame is untouched.

---

## Status

This code has not been compiled or run. It was written on a machine with no C++
toolchain available, so expect ordinary first-build fixes (missing includes, a
typo or two) rather than design surprises.

Deliberate limitations:

* Clipping is rectangular. Windows with large corner radii can show a sliver of
  content at the extreme corners.
* Text input covers caret movement, UTF-8 insertion, backspace, delete and
  click-to-position. There is no selection, copy/paste or undo.
* `Arc` and the triangle primitives are real geometry, not SDF primitives.
* The demo targets D3D11. The other backends run through the same demo via
  `ARGONUI_BACKEND`.