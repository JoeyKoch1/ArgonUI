# Architecture

ArgonUI has four layers, and the only rule that matters is that each one knows
nothing about the one below it.

```
  your code              widgets: Window, Section, Row, ToggleSwitch, ...
      |
  Context                ids, layout cursor, hover, state, animation
      |
  DrawList               primitives, batching, clipping, index rewriting
      |
  IArgonBackend          device, target, present, textures
      |
  D3D11 / D3D12 / Vulkan / OpenGL
```

A widget emits quads into a `DrawList`. It never touches a backend. The
`DrawList` never includes a GPU header. The core never includes a GPU header.
That is what makes the UI compile on a machine with no graphics drivers, and
what makes switching graphics API a link-time change.

## Context

`argon::ui::Context` owns everything that survives a frame boundary and
everything that has to be resolved during one.

| It holds | Because |
|---|---|
| The draw list | Reset each `NewFrame`. |
| The input snapshot | Copied from your platform each frame. |
| The id stack | Gives every widget a stable identity. |
| The layout stack | Cursor, content region, spacing. |
| The style stack | Per-widget and per-window theming. |
| `states_` | Toggle positions, scroll offsets, combo open flags. |
| `anims_` | Every animated scalar, keyed by id. |
| `FontManager` | One baked atlas per pixel size. |

It is the only object an application constructs directly, and it is thread-local
via `Context::Current()`.

## DrawList

The frame's entire output. You can read it, assert on it, or hand it to your own
renderer.

Two properties do the heavy lifting:

**Quads merge.** Consecutive quads with the same clip rect, texture, blend mode
and vertex flags append to the same batch. A menu frame with a few hundred
rounded rectangles, sliders, glyph runs and images lands in single-digit draw
calls. Glyph runs merge because a whole string of text is emitted under one
texture binding.

**The index buffer can be rewritten.** `InsertBackgroundBox` inserts geometry at
a recorded checkpoint, which is how a collapsible section paints its card
behind content whose height it only discovers at `EndSection`.

## The backend

One interface, `IArgonBackend`. Its whole job is upload vertices, set a
scissor rect and a texture per batch, and draw. It never sees a widget.

The interface separates three things that people need to change independently
in a real integration:

- **Device.** Whose adapter and queue?
- **Target.** Whose swapchain or framebuffer?
- **Present.** Who calls `Present`, and do we run before or after it?

That separation is the difference between a library that works in a standalone
window and one that works in both. See
[Backend ownership](backend-ownership.md).

## Why one pipeline

ArgonUI has no tessellation, no mesh cache and no geometry rebuilding. A panel,
a toggle knob, a slider thumb, an icon stroke, a glyph and a thumbnail are all
the same 64-byte vertex. The fragment shader evaluates a rounded-box signed
distance field and produces fill, border, gradient, glow and texture
modulation from it.

The practical consequences:

- A rounded, bordered card is one primitive. ImGui-style stacks of rectangles
  would be two.
- A drop shadow is a falloff in the same shader, not a blur pass.
- Anti-aliasing is `fwidth` on the SDF, correct at any scale and any DPI.
- Changing a corner radius costs nothing at runtime.

The cost is one 64-byte vertex per corner, which for a menu is negligible.

## Why containers size themselves

An immediate-mode UI draws in one pass and cannot know how tall a group is
until it is finished. ImGui resolves this by making you hand `BeginChild()` a
size up front, which means every collapsible group needs a hard-coded height and
breaks when its contents change.

ArgonUI resolves it by letting the index buffer be edited after the fact.
`BeginSection` records `DrawCheckpoint()`, the body draws normally, and
`EndSection` splices the card in at that checkpoint, shifting later batches by
six indices. Cost is O(draw calls), not O(vertices).

The visible effect is that `BeginSection` takes no height and you draw the body
unconditionally. A closed section clips its body to nothing, so there is one
layout path instead of two and no "forgot to check" flicker.

## State

Widget state is keyed by a hashed id path, not stored by the caller. `PushID`
scopes it, so a widget inside a loop has a different identity per iteration.

```cpp
ctx.PushID(i);
bool v = ctx.StateB(ctx.GetID("enabled"), false);
ToggleSwitch(&v);
ctx.PopID();
```

In practice this means your own data stays plain `bool`, `float` and `int`, and
there is nothing to serialise, save or tear down.

## Where to go next

- [The draw list](draw-list.md) for batching, clipping and the SDF model
- [The vertex format](vertex-format.md) for the 64-byte contract
- [Backend ownership](backend-ownership.md) for device, target and present
- [Immediate mode state](state.md) for ids, hover and animation
- [Writing a widget](../guides/writing-a-widget.md) to extend the set