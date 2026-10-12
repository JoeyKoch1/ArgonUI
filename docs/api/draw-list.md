# Draw list

The `DrawList` is the complete, backend-agnostic output of one ArgonUI frame. A
backend's entire job is to upload the vertices and indices, set a scissor rect
and a texture per batch, and issue `DrawIndexed`. It never sees a widget. Read
this page before writing a backend or emitting a custom primitive.

Source: [`include/argonui/Draw/DrawList.h`](../../include/argonui/Draw/DrawList.h),
implementation in [`src/Draw/DrawList.cpp`](../../src/Draw/DrawList.cpp).

## Batching

The UI core emits quads into one shared vertex and index pair and greedily
merges consecutive quads into batches whenever the clip rect, texture, blend mode
and command flags all match. A typical menu frame collapses to 3 to 8 draw calls
from a few thousand primitives, because widgets flush their text runs and shapes
in large contiguous runs.

## `BlendMode`

```cpp
enum class BlendMode : u8 {
    Alpha = 0,
    Additive = 1,
};
```

Per-batch blend state. Scoped enumeration over `u8`.

| Enumerator | Value | Backend blend | Use for |
|---|---|---|---|
| `Alpha` | 0 | `ONE, ONE_MINUS_SRC_ALPHA` | Everything by default. The fragment shader already outputs premultiplied colour. |
| `Additive` | 1 | `ONE, ONE` | Accent glows, energy bars, laser lines. |

## `CmdFlags`

```cpp
enum class CmdFlags : u8 {
    None = 0,
    WhiteTexture = 1u << 0,
    FontAtlas = 1u << 1,
};
```

Per-batch flags, distinct from the per-vertex `VF_*` flags. The field in
`DrawCmd` is a raw `u8` bitmask, so OR the values together.

| Enumerator | Value | Meaning |
|---|---|---|
| `None` | 0 | A real texture is bound. |
| `WhiteTexture` | `1u << 0` | The built-in white 1x1 is bound. This is a shape, not a glyph or image. |
| `FontAtlas` | `1u << 1` | The texture is the font atlas. The shader may skip the RGB multiply. |

## `DrawCmd`

```cpp
struct DrawCmd {
    Rect clip;          ///< Scissor rect in pixels. Already intersected by the CPU.
    u32 indexOffset;    ///< First index in DrawList::Indices().
    u32 indexCount;     ///< Number of indices.
    u32 textureId;      ///< Backend texture handle. 0 == built-in white.
    BlendMode blend;    ///< How the backend must set up the blend state.
    u8 flags;           ///< CmdFlags bitmask.
    u16 pad;

    u32 QuadCount() const { return indexCount / kIndicesPerQuad; }
};
```

One merged batch of quads, exactly 32 bytes, enforced by a `static_assert`.

| Field | Type | Offset | Meaning |
|---|---|---|---|
| `clip` | `Rect` | 0 | Scissor rect in pixels, already intersected on the CPU. |
| `indexOffset` | `u32` | 16 | First index in `DrawList::Indices()`. |
| `indexCount` | `u32` | 20 | Number of indices covered by this batch. |
| `textureId` | `u32` | 24 | Backend texture handle. 0 is the built-in white 1x1. |
| `blend` | `BlendMode` | 28 | Blend state the backend must set. |
| `flags` | `u8` | 29 | `CmdFlags` bitmask. |
| `pad` | `u16` | 30 | Unused, keeps the struct at 32 bytes. |

### `DrawCmd::QuadCount`

```cpp
u32 QuadCount() const { return indexCount / kIndicesPerQuad; }
```

**Returns:** how many quads this batch covers.

Integer division by 6. A batch built from triangles under-reports, because
triangles contribute 3 indices each.

## `DrawStats`

```cpp
struct DrawStats {
    u32 vertices = 0;
    u32 indices = 0;
    u32 quads = 0;
    u32 drawCalls = 0;   ///< == CmdCount() after the backend submits.
    u32 clipRegions = 0;
    u32 batchesMerged = 0;   ///< quads that joined an existing batch instead of starting one.
};
```

Per-frame counters, surfaced by the debug overlay. All zero by default.

| Field | Meaning |
|---|---|
| `vertices` | Vertices emitted. Set by `DrawList::Finalize`. |
| `indices` | Indices emitted. Set by `DrawList::Finalize`. |
| `quads` | Primitives emitted, counted as they are emitted. |
| `drawCalls` | Batches opened. Equals `CmdCount()` once `Finalize` has run. |
| `clipRegions` | Distinct clip rects interned. |
| `batchesMerged` | Primitives that joined an existing batch instead of starting one. Computed by `Finalize` as `quads - drawCalls`. |

`quads` and `drawCalls` are live counters during emission. The rest are only
meaningful after [`DrawList::Finalize`](#drawlistfinalize).

## `DrawList::BatchState`

```cpp
struct BatchState { u32 textureId; u8 flags; BlendMode blend; Rect clip; };
```

A snapshot of everything that decides which batch a primitive joins. A widget
that changes texture, blend or clip takes one of these on entry and puts it back
on exit, so the state it inherited survives its own body.

| Field | Meaning |
|---|---|
| `textureId` | Currently bound texture, 0 for the built-in white. |
| `flags` | Current `CmdFlags` bitmask. |
| `blend` | Current `BlendMode`. |
| `clip` | Clip rect that was active when the snapshot was taken. |

## Lifetime

### `DrawList::DrawList`

```cpp
DrawList();
```

Creates an empty list with a default clip rect of `(-1e6, -1e6, 1e6, 1e6)`, which
is effectively infinite, and reserves 8192 vertices, 12288 indices and 256
commands. Backends clamp the scissor against the framebuffer, so the CPU side
never needs to know the display size.

### `DrawList::Reset`

```cpp
void Reset();
```

**Returns:** nothing, it clears the list in place.

Drops all geometry, resets the clip, texture, flags and blend to their defaults,
clears `stats_` and sets `clipRegions` to 1. Capacity is retained, so a list that
survives many frames stops allocating after the first.

### `DrawList::Reserve`

```cpp
void Reserve(size_t vertexCapacity, size_t indexCapacity, size_t cmdCapacity);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `vertexCapacity` | `size_t` | | Vertices to preallocate. |
| `indexCapacity` | `size_t` | | Indices to preallocate. |
| `cmdCapacity` | `size_t` | | Commands to preallocate. Also reserves 64 clip rects. |

**Returns:** nothing, it grows the backing storage without emitting anything.

Call it once at startup with a rough high-water mark. The vectors still grow
past these values if a frame needs more, so this is an optimization and not a
limit.

## Clipping

### `DrawList::PushClip`

```cpp
Rect PushClip(const Rect& rect);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `rect` | `const Rect&` | | Rect to intersect with the current clip. |

**Returns:** the clip that was active before this call.

The new clip is `IntersectStrict` of the current clip and `rect`, so a push
outside the parent clips to nothing rather than escaping it. The new rect is
interned into the clip list, and the open batch is closed so the next primitive
starts a new command.

{% hint style="warning" %}
The return value must be handed back to `PopClip`. It is the only record of the
previous clip, and the interned clip table is append-only, so a lost value means
a subtree cannot be popped without ending every clip below it.
{% endhint %}

**Example**

```cpp
const Rect previous = dl.PushClip(visible);
// ... draw children ...
dl.PopClip(previous);
```

### `DrawList::PopClip`

```cpp
void PopClip(const Rect& previousClip);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `previousClip` | `const Rect&` | | The value `PushClip` returned. |

**Returns:** nothing, it restores the clip.

The rect is not interned on the way back in, which is fine because `PushClip`
already interned it.

### `DrawList::CurrentClip`

```cpp
const Rect& CurrentClip() const { return clip_; }
```

**Returns:** a reference to the active clip rect.

The reference stays valid until the next call that changes the clip.

## Texture and blend state

### `DrawList::SetTexture`

```cpp
void SetTexture(u32 textureId, CmdFlags flags);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `textureId` | `u32` | | Backend texture handle, 0 for the built-in white. |
| `flags` | `CmdFlags` | | Batch-level flags describing the texture. |

**Returns:** nothing, it sets the bound texture.

A no-op when the texture and flags are already current, so calling it every
frame costs two comparisons. Any change closes the open batch.

### `DrawList::SetBlend`

```cpp
void SetBlend(BlendMode blend);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `blend` | `BlendMode` | | Blend mode for subsequent primitives. |

**Returns:** nothing, it sets the blend mode.

A no-op when unchanged.

### `DrawList::SaveBatchState`

```cpp
BatchState SaveBatchState() const;
```

**Returns:** a snapshot of the texture, flags, blend and clip.

{% hint style="info" %}
`BatchState` is a plain struct with no destructor and no ownership. It is safe to
store on the widget stack for the duration of a widget body.
{% endhint %}

### `DrawList::RestoreBatchState`

```cpp
void RestoreBatchState(const BatchState& s);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `s` | `const BatchState&` | | A value from `SaveBatchState`. |

**Returns:** nothing, it puts the texture, flags, blend and clip back.

It assigns unconditionally, so restoring an identical snapshot still closes the
open batch. Compare first if you are restoring in a hot loop.

## Primitives

Geometry that would be entirely scissored away never reaches the vertex buffer.
`AddBox`, `AddBoxRotated`, `AddLine` and `AddCircle` test the clip rect up front
and return early, and `EmitQuad` drops a box whose core has collapsed to
nothing. `AddImage`, `AddTriangle`, `AddArc` and `AddGlyphQuad` rely on the
scissor instead, so an off-screen arc or polygon still costs its triangles.

Call [`DrawList::IsClipped`](#drawlistisclipped) yourself before an expensive
widget body.

### `DrawList::AddBox`

```cpp
void AddBox(const Rect& bounds,
            const Color& fill,
            const CornerRadius& radius = CornerRadius(),
            f32 borderWidth = 0.0f,
            const Color& borderColor = Color(0, 0, 0, 0),
            f32 glowWidth = 0.0f,
            const Color& glowColor = Color(0, 0, 0, 0),
            u32 vertexFlags = VF_FLAT);
```

The general-purpose primitive: a rounded box with a fill, an inner border, an
optional gradient and an optional outer glow. Everything else funnels here.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Screen-space rectangle in pixels. |
| `fill` | `const Color&` | | Interior colour. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Per-corner radii, clockwise from top-left. |
| `borderWidth` | `f32` | `0.0f` | Inner border thickness in pixels. 0 draws no border. |
| `borderColor` | `const Color&` | `Color(0, 0, 0, 0)` | Border colour. Also the far stop of a gradient. |
| `glowWidth` | `f32` | `0.0f` | Outer glow spread in pixels. 0 draws no glow. |
| `glowColor` | `const Color&` | `Color(0, 0, 0, 0)` | Glow colour. |
| `vertexFlags` | `u32` | `VF_FLAT` | Any of the `VF_*` flags from `Vertex.h`. |

**Returns:** nothing, it appends to the draw list.

Two behaviours worth knowing. A border with `borderWidth > 0` but a fully
transparent `borderColor` picks up the fill colour, so a caller that sets the
width without the colour still gets a visible border. Radii are clamped against
the box after the glow has been removed, which keeps the distance field from
folding over itself and keeps the glow band even.

{% hint style="info" %}
The glow expands the emitted quad by `glowWidth` on every side, so the extra is
outside `bounds`. The draw list accounts for that when it tests the clip, but
your own hit-testing and layout padding have to add it themselves.
{% endhint %}

**Example**

```cpp
dl.AddBox(card, ctx.Color(StyleColorId::CardBg),
          CornerRadius(8.0f),
          1.0f, ctx.Color(StyleColorId::CardBorder));
```

### `DrawList::AddBoxRotated`

```cpp
void AddBoxRotated(const Rect& bounds, f32 angleRadians,
                   const Color& fill,
                   const CornerRadius& radius = CornerRadius(),
                   f32 borderWidth = 0.0f,
                   const Color& borderColor = Color(0, 0, 0, 0),
                   f32 glowWidth = 0.0f,
                   const Color& glowColor = Color(0, 0, 0, 0),
                   u32 vertexFlags = VF_FLAT);
```

`AddBox` rotated about the box's own centre.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Screen-space rectangle in pixels, before rotation. |
| `angleRadians` | `f32` | | Rotation about the centre. Positive is clockwise on screen. |
| `fill` | `const Color&` | | Interior colour. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Per-corner radii in the box's own frame. |
| `borderWidth` | `f32` | `0.0f` | Inner border thickness in pixels. |
| `borderColor` | `const Color&` | `Color(0, 0, 0, 0)` | Border colour, or the gradient's far stop. |
| `glowWidth` | `f32` | `0.0f` | Outer glow spread in pixels. |
| `glowColor` | `const Color&` | `Color(0, 0, 0, 0)` | Glow colour. |
| `vertexFlags` | `u32` | `VF_FLAT` | Any of the `VF_*` flags. |

**Returns:** nothing, it appends to the draw list.

Rotation is free. The fragment shader rebuilds its local coordinates from the
interpolated `u` and `v`, so a rotated quad is a quad whose four corners were
rotated on the CPU. No extra vertices, no matrix uniform, no second pipeline.

Radii stay circular in the box's own frame, which is exactly what a stroke or a
capsule wants: a corner arc rotates with the box instead of being sheared by the
rotation.

`bounds` is the unrotated rectangle. An angle within `1e-4` of zero skips the
rotation maths entirely.

### `DrawList::AddLine`

```cpp
void AddLine(const Vec2& a, const Vec2& b,
             f32 thickness, const Color& color, u32 vertexFlags = VF_FLAT);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `const Vec2&` | | Start point in pixels. |
| `b` | `const Vec2&` | | End point in pixels. |
| `thickness` | `f32` | | Stroke width in pixels. Clamped to at least 0.5. |
| `color` | `const Color&` | | Stroke colour. |
| `vertexFlags` | `u32` | `VF_FLAT` | Any of the `VF_*` flags. |

**Returns:** nothing, it appends to the draw list.

A true round-capped capsule at any angle, not a bounding-box approximation. The
implementation builds a box whose long axis is `a` to `b` with every corner
radius set to half the thickness, then rotates it so its local Y runs along the
segment. Because the radii stay circular in the box's own frame, the caps are
exactly semicircular however the segment is oriented. A diagonal stroke is one
quad with a real capsule silhouette, at the cost of one quad.

{% hint style="success" %}
This is what makes hairlines and dividers at arbitrary angles cheap. A
conventional renderer would either step the line out of many small quads or
approximate the caps with a separate circle primitive. Here both ends are the
same SDF the box uses, so a stroked path is one primitive per segment with
analytic anti-aliasing at the caps.
{% endhint %}

Degenerate input is handled: a zero-length segment becomes a dot of the same
thickness rather than disappearing, and a thickness below half a pixel is raised
to it so the line cannot vanish.

### `DrawList::AddCircle`

```cpp
void AddCircle(const Rect& bounds, const Color& fill,
               f32 borderWidth = 0.0f,
               const Color& borderColor = Color(0, 0, 0, 0));
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Bounding box. The circle is inscribed in it. |
| `fill` | `const Color&` | | Interior colour. |
| `borderWidth` | `f32` | `0.0f` | Inner border thickness in pixels. |
| `borderColor` | `const Color&` | `Color(0, 0, 0, 0)` | Border colour. |

**Returns:** nothing, it appends to the draw list.

The radius is half the smaller side of `bounds`, so a non-square box gives you a
circle in the middle of it, not an ellipse. A box of `2r` by `2r` gives a circle
of radius `r`.

Implemented as a single `AddBox` with every corner radius set to that radius.

### `DrawList::AddArc`

```cpp
void AddArc(const Rect& bounds, f32 startDeg, f32 sweepDeg, f32 thickness,
            const Color& color);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Bounding box. The arc is centred in it. |
| `startDeg` | `f32` | | Start angle in degrees, clockwise from 3 o'clock. |
| `sweepDeg` | `f32` | | Sweep in degrees. Negative sweeps anticlockwise. |
| `thickness` | `f32` | | Ring thickness in pixels, measured inward. |
| `color` | `const Color&` | | Ring colour. |

**Returns:** nothing, it appends to the draw list.

A ring segment around the centre of `bounds`, at outer radius half the smaller
side. `startDeg == 0` is 3 o'clock and `sweepDeg == 360` is a full ring. A sweep
past 360 keeps going, so it does not wrap.

This is the one primitive that is not a single quad. It is tessellated into
triangle pairs, at roughly 6 degrees per step and never fewer than 2 steps, and
a thickness larger than the radius collapses to a filled sector with an inner
radius of zero. Used by radial gauges and the arc accents in the reference UI.

{% hint style="info" %}
Degrees here, radians everywhere else. Angles are degrees because a gauge angle
is read off a design in degrees, and the conversion is confined to this one
function.
{% endhint %}

### `DrawList::AddImage`

```cpp
void AddImage(const Rect& bounds,
              u32 textureId,
              const Color& tint = Color(1, 1, 1, 1),
              const CornerRadius& radius = CornerRadius(),
              const Rect& uvRect = Rect(0, 0, 1, 1));
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Destination rectangle in pixels. |
| `textureId` | `u32` | | Backend texture handle. 0 emits nothing. |
| `tint` | `const Color&` | `Color(1, 1, 1, 1)` | Multiplies the sampled texture, RGB and alpha. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Rounded clipping applied to the image. |
| `uvRect` | `const Rect&` | `Rect(0, 0, 1, 1)` | Sub-rectangle of the texture to sample. |

**Returns:** nothing, it appends to the draw list.

Still one quad. The rounded corners are part of the same signed distance field,
so a circular avatar is one primitive rather than a quad behind a stencil. The
batch is emitted with `CmdFlags::None` because a real texture is bound.

{% hint style="warning" %}
Textures are uploaded with straight, non-premultiplied alpha. The shader computes
`alpha = shapeMask * shapeAlpha * tex.a` and `rgb = shapeColor * alpha * tex.rgb`
and outputs premultiplied colour itself. Upload your pixels verbatim; if you
premultiply on the CPU the edges of every image double-darken.
{% endhint %}

### `DrawList::AddTriangle`

```cpp
void AddTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `const Vec2&` | | First vertex in pixels. |
| `b` | `const Vec2&` | | Second vertex in pixels. |
| `c` | `const Vec2&` | | Third vertex in pixels. |
| `color` | `const Color&` | | Flat fill colour. |

**Returns:** nothing, it appends to the draw list.

For the rare cases that genuinely are not a box: arrows, chevrons drawn as
polygons, progress spikes. Winding is not culled, so the order of `a`, `b`, `c`
does not affect visibility.

Even here the vertex format is unchanged. The three vertices carry a degenerate
core box sized to the triangle's bounding box, the distance field degenerates to
a filled box, and the rasterizer clips it to the triangle.

### `DrawList::IsClipped`

```cpp
bool IsClipped(const Rect& bounds) const;
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Bounds to test, usually including any glow expansion. |

**Returns:** true when `bounds` has no area inside the current clip.

This is a scissor-only helper. It emits nothing and is the supported way to skip
a subtree or an expensive computation when the widget is off screen.

{% hint style="warning" %}
The name reads as "is any of this clipped", but the answer is true only when the
rect is *entirely* clipped away. Anything partially visible returns false.
{% endhint %}

### `DrawList::IsFullyClipped`

```cpp
bool IsFullyClipped() const { return clip_.IsEmpty(); }
```

**Returns:** true when the current clip itself is empty.

Cheaper than `IsClipped`, and the right test after a push that may have collapsed
the clip to nothing.

### `DrawList::AddGlyphQuad`

```cpp
void AddGlyphQuad(const Rect& bounds,
                  const Rect& uv,
                  const Color& color);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Destination rect in pixels. |
| `uv` | `const Rect&` | | Sub-rectangle of the atlas holding this glyph. |
| `color` | `const Color&` | | Text colour, including alpha. |

**Returns:** nothing, it appends to the draw list.

One glyph, one quad. Call [`DrawList::SetTexture`](#drawlistsettexture) with the
font atlas first: this method uses whichever texture and flags are currently
bound rather than taking them as parameters, which is what lets a run of
consecutive glyphs merge into one batch.

The quad is emitted with `VF_TEX_COVERAGE`, so the shader reads coverage from the
texel's red channel and forces the sampled RGB to white. Font atlases are `R8`,
a quarter the memory of `RGBA8` for the texture that dominates VRAM in a
text-heavy menu.

## Painting behind content

Two methods let a container draw its background after its children, when the
size is finally known.

### `DrawList::IndexCheckpoint`

```cpp
u32 IndexCheckpoint() const { return static_cast<u32>(indices_.size()); }
```

**Returns:** the current index count, as a position in the index buffer.

Take one in a Begin function, before any children are emitted, and pass it to
[`DrawList::InsertBackgroundBox`](#drawlistinsertbackgroundbox) in the matching
End function.

### `DrawList::InsertBackgroundBox`

```cpp
void InsertBackgroundBox(const Rect& bounds, const CornerRadius& radius, const Color& fill,
                         f32 borderWidth, const Color& border, f32 glowWidth,
                         const Color& glowColor, u32 checkpoint, const Rect& clip);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Background rectangle in pixels. |
| `radius` | `const CornerRadius&` | | Per-corner radii for the background. |
| `fill` | `const Color&` | | Background fill colour. |
| `borderWidth` | `f32` | | Border thickness in pixels. |
| `border` | `const Color&` | | Border colour. |
| `glowWidth` | `f32` | | Outer glow spread in pixels. |
| `glowColor` | `const Color&` | | Glow colour. |
| `checkpoint` | `u32` | | A value from `IndexCheckpoint`. |
| `clip` | `const Rect&` | | The clip that was active at `checkpoint`. |

**Returns:** nothing, it splices geometry into the index buffer.

This is what lets a container size itself to its content. A collapsible section,
a card sized to its rows, an expanding group: the height is only known at End
time, so the background is inserted at the position the children started from,
which places it behind them. Immediate-mode libraries built on child windows make
the caller declare a size up front instead.

The mechanism is an index insertion, not a buffer rewrite. The quad's four
vertices are appended to the end of the vertex array, where nothing references
them yet. Its six indices are then inserted into the index buffer at
`checkpoint`, which pushes every index after that point along by
`kIndicesPerQuad`, and every existing batch whose `indexOffset` is at or after
`checkpoint` is shifted by the same amount. Since primitives inside one batch
are consecutive, shifting the batch start by 6 is enough to shift all of them.
The cost is O(draw calls), not O(vertices).

{% hint style="warning" %}
`clip` must be the clip that was active when the checkpoint was taken, not the
one active now. A subtree that pushed a clip at Begin and popped it at End
restores the outer clip before this call, so the outer clip is the wrong one.
Getting it wrong paints the background with the wrong scissor and nothing tells
you why.
{% endhint %}

The call is a no-op if `bounds` is empty, if `checkpoint` is past the end of the
index buffer, or if the background would be entirely clipped away. When the
batch immediately before the checkpoint happens to be a white, alpha-blended
shape batch under the same clip, the background is folded into it instead of
starting a new command, so the insert is free in the common case.

**Example**

```cpp
// Begin
const u32 checkpoint = ctx.DrawCheckpoint();
const Rect clip = ctx.ClipRect();
// ... emit any number of rows ...
// End
ctx.MutableDrawList().InsertBackgroundBox(
    card, CornerRadius(radius), ctx.Color(StyleColorId::CardBg),
    1.0f, ctx.Color(StyleColorId::CardBorder), 0.0f, Color(0, 0, 0, 0),
    checkpoint, clip);
```

## Accessors

All of these are `const` and free of side effects. The pointers are valid until
the next call that mutates the list, including `Reset` and `Finalize`.

| Method | Returns |
|---|---|
| `Vertices()` | `const Vertex*`, first vertex. |
| `VertexCount()` | `u32`, number of vertices emitted. |
| `VertexCapacity()` | `size_t`, number of vertices emitted. |
| `Indices()` | `const u32*`, first index. |
| `IndexCount()` | `u32`, number of indices emitted. |
| `IndexCapacity()` | `size_t`, number of indices emitted. |
| `Cmds()` | `const DrawCmd*`, first command. |
| `CmdCount()` | `u32`, number of commands. |
| `CmdCapacity()` | `size_t`, number of commands. |
| `ClipRegion(index)` | `const Rect&`, the interned clip rect at `index`. |
| `ClipRegionCount()` | `u32`, number of interned clip rects. |
| `Clips()` | `const Rect*`, first interned clip rect. |
| `Stats()` | `const DrawStats&`, this frame's counters. |

{% hint style="warning" %}
The three `*Capacity` accessors return the element count, not the reserved
capacity. Use `VertexCount()`, `IndexCount()` and `CmdCount()` for the counts;
the capacity names report the same numbers today and are not a separate
quantity.
{% endhint %}

`ClipRegion` indexes into the interned table, not into the commands. The first
entry is always the infinite default clip, which is why the count starts at 1.

### `DrawList::UploadBytes`

```cpp
size_t UploadBytes() const {
    return vertices_.size() * sizeof(Vertex) + indices_.size() * sizeof(u32);
}
```

**Returns:** the total bytes the backend would upload this frame.

Vertices at 64 bytes each plus 4 per index. This is the number to watch when
profiling: a frame that suddenly costs megabytes is usually a widget that has
escaped its clip rect and is emitting content nobody can see.

### `DrawList::Finalize`

```cpp
void Finalize();
```

**Returns:** nothing, it closes the frame out.

Called once by the `Context` before submission, never by user code. It fills in
`stats_.vertices`, `stats_.indices`, `stats_.clipRegions` and
`stats_.batchesMerged`, drops any degenerate trailing batch left by a caller that
changed texture or blend after emitting geometry, and sets `stats_.drawCalls` to
the final command count.

Batch merging happens during emission, in `OpenBatch`, not here. `Finalize` only
removes trailing batches that ended up with zero indices, which happens when a
caller sets a texture or blend after drawing with the previous one.

{% hint style="warning" %}
After `Finalize`, `CmdCount()` may be smaller than the value `drawCalls` held
during emission, and `BatchState` values captured earlier are stale. A backend
must submit after this call and never before it.
{% endhint %}

## See also

- [Vertex format](vertex.md) - the 64-byte record this builds.
- [Geometry](geometry.md) - `Rect` and `CornerRadius`, the inputs to every primitive.
- [Colour](color.md) - the fill, border and glow parameters.
- [Backend interface](backend.md) - the consumer of everything on this page.