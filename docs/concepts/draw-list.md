# The draw list

`DrawList` is one frame's entire output. Widgets append primitives to it, the
backend uploads it, and nothing else exists in between.

## Primitives are rounded boxes

There is no triangle mesh and no tessellation. Every primitive is a quad
carrying a rounded-box signed distance field, and the fragment shader evaluates
it analytically. That one decision is why a bordered panel costs one draw and a
shadow costs nothing.

| Call | Emits |
|---|---|
| `AddBox` | Rounded rect, optional fill, border, gradient, glow. |
| `AddBoxRotated` | The same, rotated. Free, see below. |
| `AddLine` | A true round-capped capsule at any angle. |
| `AddCircle` | A box with all four radii at half the width. |
| `AddArc` | A ring segment as real triangles. |
| `AddImage` | A textured quad clipped to a rounded rect. |
| `AddGlyphQuad` | One glyph, coverage-sampled. |
| `AddTriangle` | Escape hatch. |

Almost everything a menu needs is `AddBox` with different arguments. A slider
thumb is a circle. A toggle track is a box with radius at half its height. A
card is a box with a border and a shadow.

## Rotation is free

`AddBoxRotated` takes an angle. It costs nothing extra, because the shader
reconstructs its local coordinates from the interpolated UV rather than from
screen position. A rotated quad is four corners rotated on the CPU, and the SDF
is evaluated in the box's own frame.

The consequence is that `AddLine` is a real round-capped stroke at any angle,
not a bounding-box approximation. That matters for diagonal icon strokes and
angled HUD elements, where an approximation is visible.

Corner radii stay circular in the box's own frame, which is what you want for a
rotated stroke and slightly wrong for a heavily rotated panel. Do not rotate
panels.

## Batching

`DrawList` merges consecutive quads whenever all four of these match:

- clip rect
- texture id
- blend mode
- vertex flags

The merged run becomes one `DrawCmd`. Batches are emitted in order and a
backend draws them in order, so there is no sorting step and no z-buffer.

What makes merging work in practice is that widgets emit their geometry in
runs rather than as isolated items. A string of text sets the glyph atlas once
and emits every glyph under it. A list of identical rows emits N rounded rects
with nothing between them. The clip rect is the constraint that hurts most,
which is why scroll areas clip once at the area rather than per widget.

A typical menu frame:

```
ArgonUI, one real menu frame:
  quads ~1400   verts ~5600   draw calls 6   upload 352 KB
```

## Clipping

```cpp
ctx.PushClipRect(rect);   // intersects with the current clip
// ... draw ...
ctx.PopClipRect();
```

The clip is a scissor rect on the CPU side, applied per batch rather than per
pixel. Because it is part of the batch key, changing it starts a new batch, so
nesting clip regions costs batches rather than fill.

Clipping is rectangular. A window with large corner radii can show a sliver of
content at the extreme corners. ArgonUI accepts that trade because a rounded
per-pixel clip would have to run in the fragment shader and would fight the
analytic AA.

## Writing behind already-emitted geometry

This is what makes self-sizing containers possible.

```cpp
const u32 checkpoint = ctx.DrawCheckpoint();
// ... emit a whole group of content ...
ctx.MutableDrawList().InsertBackgroundBox(
    cardBounds, CornerRadius(12.0f), cardBg, 1.0f, cardBorder,
    0.0f, Color(0, 0, 0, 0), checkpoint, clipAtCheckpoint);
```

`InsertBackgroundBox` appends four vertices, then inserts six indices at the
checkpoint and shifts every later batch's index offset by six. Nothing already
emitted moves, and no geometry is drawn twice. Cost is O(draw calls).

`EndSection` uses this. It learns the body height at the end, then paints the
card behind it. See [Immediate mode state](state.md) for why that matters.

## Reading it

`DrawList` is inspectable after a frame, which is how the null backend and the
metrics overlay work:

```cpp
const DrawStats& s = ctx.GetDrawList().Stats();
s.drawCalls;    // batches
s.quads;        // primitives
s.vertices;
s.clipRegions;  // distinct scissor rects
```

For a regression test, assert on draw calls. A frame that grows from 6 to 60
draw calls after a refactor has broken a merge somewhere, usually by changing
the clip rect more often than necessary.

## Blending

Two modes:

- `Alpha`, premultiplied source alpha, `(ONE, ONE_MINUS_SRC_ALPHA)`. Everything
  uses this by default.
- `Additive`, `(ONE, ONE)`. For accent glows, tracer lines, energy bars.

Backends implement both as pipeline objects created at init, so switching is a
pointer swap, not state creation.

## See also

- [The vertex format](vertex-format.md)
- [Draw list reference](../api/draw-list.md)