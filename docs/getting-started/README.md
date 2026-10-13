# ArgonUI

An immediate-mode interface toolkit for in-game menus.

You describe the UI as a function that runs every frame, the way you would with
ImGui. ArgonUI differs in how the pixels get there: one draw list, one shader,
one pipeline per graphics API, and a backend interface that separates owning a
device from owning a target from owning the present.

That combination is aimed at a specific problem. A menu drawn over a running
game has to be cheap, has to survive whatever the host did to the graphics
device last frame, and should not drag a 4 MB single-header library into a
project that already has three rendering abstractions in it.

## Where to start

| If you want to | Read |
|---|---|
| Get something on screen | [Your first window](first-window.md) |
| Understand the design | [Architecture](../concepts/architecture.md) |
| Draw into a game's device | [Rendering into someone else's device](../guides/overlay.md) |
| Look up a function | [API reference](../api/core-types.md) |

## What is in the box

- **One pipeline.** Every primitive is the same 64-byte vertex carrying a
  rounded-box signed distance field. A bordered panel is one primitive, not two
  stacked rectangles, and a drop shadow needs no blur pass. Anti-aliasing is
  analytic, so it is correct at any scale.
- **Batched submission.** Quads sharing clip rect, texture, blend and flags
  merge. A typical menu frame lands in single-digit draw calls.
- **Four backends.** Direct3D 11, Direct3D 12, Vulkan and OpenGL 3.3, each an
  independent library. A headless null backend ships for tests.
- **Self-measuring containers.** Collapsible sections size themselves. You never
  hand `BeginChild` a height.
- **Crisp text at any DPI.** Glyphs are baked into per-size R8 atlases, so
  150% scaling renders sharp rather than upscaled.
- **Around 60 procedural icons.** Drawn from the same primitives as everything
  else, so they inherit hover state and stay sharp at any size.

## A minimal frame

```cpp
#include "argonui/ArgonUI.h"

argon::ui::Context ctx;
argon::ui::IArgonBackend* backend = argon::ui::CreateBackend(GraphicsApi::D3D11);
ctx.Initialize(backend);

while (running) {
    argon::ui::FrameState fs;
    fs.displaySize = { (float)clientWidth, (float)clientHeight };
    fs.deltaTime   = dt;

    ctx.NewFrame(fs);
    ctx.BeginLayout(argon::ui::Rect(0, 0, clientWidth, clientHeight));

    if (argon::ui::BeginWindow(ctx, "main", "Settings", {900, 600})) {
        bool open = true;
        argon::ui::BeginSection(ctx, "Visuals", &open);
            argon::ui::RowToggle("ESP", &esp);
            argon::ui::RowSlider("FOV", &fov, 60.0f, 130.0f, "%.0f");
        argon::ui::EndSection(ctx);
        argon::ui::Button("Save", argon::ui::ButtonKind::Primary);
    }

    argon::ui::RenderTooltips(ctx);
    ctx.EndLayout();
    ctx.Render();
}
```

## Status

It builds, links and runs. Verified on Windows 11 with GCC 16 (MSYS2 UCRT64),
`-Wall -Wextra`, zero warnings, plus a headless suite of 26 checks covering the
backend contract, the vertex contract, batching, every tab, button interaction
and font baking. Run it with `ctest --test-dir build -C Release`.

Two things the build does not cover. The D3D12 backend needs the real Windows
SDK: MinGW-w64's `d3d12.h` predates the descriptor heap type split, so it cannot
compile a modern D3D12 backend. And nobody has yet compared a rendered frame
against the reference. See [Status](README.md#status) for the known limitations.