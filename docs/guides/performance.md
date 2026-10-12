# Performance

ArgonUI's cost model is simple: one draw list, one pipeline, a few draw calls,
and a font atlas that uploads when it changes. This page covers where the cost
actually goes and what to do about it.

## Where the time goes

| Cost | Size | Notes |
|---|---|---|
| Draw calls | 4 to 10 per menu frame | After batching. The number that matters. |
| Vertex upload | 200 to 400 KB per frame | The whole draw list, re-uploaded every frame. |
| Glyph atlas upload | Once per new glyph | Not per frame. |
| CPU layout | Microseconds | Every widget runs every frame. |
| Context state save | 1 MB memcpy | Only when you adopted a host device. |

For an overlay running alongside a game, none of these are the bottleneck. The
bottleneck is usually that you are calling the hook every frame whether or not
anyone can see the menu.

## Batching

Consecutive quads merge when clip rect, texture, blend and flags all match. That
is why a menu lands in single-digit draw calls even with a thousand primitives.

What breaks a merge:

**Changing the clip rect often.** Every change is a new batch. This is the main
cause of a frame going from 6 draw calls to 60. Clip once per scroll area, not
per row.

**Mixing textures.** Shapes use the built-in white 1x1 and text uses a glyph
atlas, so a shape and a text run cannot merge. Emit them as long runs rather
than interleaving them.

**Mixing blend modes.** `Alpha` and `Additive` are separate pipelines. An accent
glow in the middle of a list of flat shapes splits the list into three batches.

### Measuring it

```cpp
const DrawStats& s = ctx.GetDrawList().Stats();
ARGON_INFO("draws", "%u  quads %u  clip regions %u",
           s.drawCalls, s.quads, s.clipRegions);
```

Or turn on the built-in overlay:

```cpp
ctx.SetMetricsVisible(true);
```

It prints frame count, CPU milliseconds, draw calls, quads, vertices, clip
regions, merged batches, upload size and the active backend. Toggle it with a
keybinding while developing and leave it off in release.

A useful regression assertion for a test:

```cpp
assert(ctx.GetDrawList().Stats().drawCalls < 25);
```

## Do not draw when nobody is looking

The single biggest win for an overlay is not drawing a hidden menu.

```cpp
if (menuVisible && foregroundWindowIsOurs()) {
    ctx.NewFrame(fs);
    // ... widgets ...
    ctx.Render();
}
```

Set `FrameState::occluded` when the window is minimised or on another desktop.
`BeginFrame` returns false then and the backend skips the frame, while the CPU
still runs your widget tree. If you want to skip that too, guard the whole block
on your own visibility flag.

## Font atlases

Atlas uploads happen when a glyph is baked, not per frame. The first frame
showing a new string does one upload. Pre-warm by drawing the strings you use
once during startup:

```cpp
ctx.FontAt(14.0f)->MeasureText(warmupString, len);  // bakes the glyphs
```

Baking is per pixel size, so a menu that uses six sizes bakes six atlases. Ask
for them during the first frame rather than discovering them mid-animation.

Ad atlases are R8. That is a quarter of the RGBA8 cost and text is most of a
menu.

## Overlays

With `DeviceSource::External` the D3D11 backend saves and restores the full
device context state around its frame, about 1 MB of copies. If you measure and
it matters, the trade is correctness: without it, the host's next frame uses
whatever state you left behind.

## Reducing geometry

- Fewer primitives. A rounded border and a fill is one `AddBox` with
  `borderWidth` set, not two boxes.
- Skip the glow on controls that do not need it. A glow expands the quad by its
  width, which costs fill rate.
- Use `Color(0, 0, 0, 0)` for a fill you do not want rather than skipping the
  call, since the shader discards fully transparent pixels anyway.

## CPU layout

Every widget runs every frame. That is immediate mode and there is no fix. If a
frame gets expensive, the causes are usually:

- A widget inside a loop that should have been `PushID`-scoped but instead
  rebuilds a container every call
- A text measure in a hot path where the width never changes
- A layout that wraps a large list when a scroll area would let the items reuse
  last frame's measured sizes

Wrapping in a `BeginScrollArea` means off-screen items still run their widget
code. That is correct behaviour, and the reason not to put thousands of rows in
one screen.

## Threading

`Context` is thread-local for the *current* pointer, but a context is not safe
to use from two threads at once. Drive it from one thread. A background thread
that needs to render should own its own context and its own backend, then
hand over a texture.

## See also

- [The draw list](../concepts/draw-list.md)
- [DPI and scaling](../concepts/dpi.md)