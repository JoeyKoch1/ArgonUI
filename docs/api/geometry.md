# Geometry

`Rect` is the axis-aligned rectangle every layout and every draw call is built
from, and `CornerRadius` is the per-corner description that ArgonUI's rounded-box
shader consumes. Read this page if you are doing layout maths, clipping
rectangles, or writing a custom primitive.

Source: [`include/argonui/Core/Rect.h`](../../include/argonui/Core/Rect.h).

## `Rect`

```cpp
struct Rect {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};
};
```

A rectangle stored as its minimum and maximum corner, not as origin plus size.
That form is what GPU pipelines and scissor APIs want, and it makes intersecting
two rects two `Min` and two `Max` calls.

{% hint style="info" %}
`min` is the top-left corner and `max` is the bottom-right. Y grows downward, so
`min.y` is the smaller value. Nothing in the type enforces that ordering, which
is why [`Rect::Intersect`](#rectintersect) exists.
{% endhint %}

| Constructor | `constexpr` | Meaning |
|---|---|---|
| `Rect()` | yes | Degenerate rect at the origin. `IsEmpty()` is true. |
| `Rect(f32 l, f32 t, f32 r, f32 b)` | yes | From left, top, right, bottom in pixels. |
| `Rect(const Vec2& mn, const Vec2& mx)` | yes | From two corners. |
| `Rect(const Vec2& size)` | **no** | `explicit`. From a size, anchored at the origin. |

### Size and area

All four are `constexpr`.

#### `Rect::Width`

```cpp
constexpr f32 Width() const { return max.x - min.x; }
```

**Returns:** horizontal extent in pixels. Negative if `max.x < min.x`.

#### `Rect::Height`

```cpp
constexpr f32 Height() const { return max.y - min.y; }
```

**Returns:** vertical extent in pixels. Negative if `max.y < min.y`.

#### `Rect::Size`

```cpp
constexpr Vec2 Size() const { return {max.x - min.x, max.y - min.y}; }
```

**Returns:** the extent as a `Vec2`.

#### `Rect::Area`

```cpp
constexpr f32 Area() const { return Width() * Height(); }
```

**Returns:** signed area. Negative for an inverted rect, which is a cheap way to
test whether an intersection produced a valid result.

### Corners

All five are `constexpr`.

#### `Rect::GetCenter`

```cpp
constexpr Vec2 GetCenter() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
```

**Returns:** the midpoint of the diagonal.

#### `Rect::GetTopLeft`

```cpp
constexpr Vec2 GetTopLeft() const { return min; }
```

**Returns:** `min`.

#### `Rect::GetTopRight`

```cpp
constexpr Vec2 GetTopRight() const { return {max.x, min.y}; }
```

**Returns:** the corner at maximum X, minimum Y.

#### `Rect::GetBottomRight`

```cpp
constexpr Vec2 GetBottomRight() const { return max; }
```

**Returns:** `max`.

#### `Rect::GetBottomLeft`

```cpp
constexpr Vec2 GetBottomLeft() const { return {min.x, max.y}; }
```

**Returns:** the corner at minimum X, maximum Y.

### Edge slices

Each of these returns a rect covering part of the original. All eight are
`constexpr` and all of them can produce a negative extent if you ask for a strip
wider than the rect, so clamp `w` or `h` yourself when the input is untrusted.

| Method | Parameters | Result |
|---|---|---|
| `Left(w)` | `w`: `f32`, width in px | Leftmost `w` pixels, full height. |
| `Right(w)` | `w`: `f32`, width in px | Rightmost `w` pixels, full height. |
| `Top(h)` | `h`: `f32`, height in px | Topmost `h` pixels, full width. |
| `Bottom(h)` | `h`: `f32`, height in px | Bottommost `h` pixels, full width. |
| `TopLeft(sz)` | `sz`: `const Vec2&` | Corner rect of that size at `min`. |
| `TopRight(sz)` | `sz`: `const Vec2&` | Corner rect of that size at the top right. |
| `BottomRight(sz)` | `sz`: `const Vec2&` | Corner rect of that size at `max`. |
| `BottomLeft(sz)` | `sz`: `const Vec2&` | Corner rect of that size at the bottom left. |

None of them take a default argument.

### Transforms

All five are `constexpr`.

#### `Rect::Expand`

```cpp
constexpr Rect Expand(f32 amount) const { return {min.x - amount, min.y - amount, max.x + amount, max.y + amount}; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `amount` | `f32` | | Pixels to add outward on every side. Negative shrinks. |

**Returns:** a rect grown by `amount` in each direction.

Expanding by `n` makes the rect `2n` larger in both dimensions, since every
edge moves.

#### `Rect::Shrink`

```cpp
constexpr Rect Shrink(f32 amount) const { return Expand(-amount); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `amount` | `f32` | | Pixels to remove from every side. Negative grows. |

**Returns:** `Expand(-amount)`.

This is the inverse of `Expand` and the pair reads more clearly than a
negated literal. [`DrawList::AddBox`](draw-list.md) uses it internally to peel
the glow spread off a box before measuring the core silhouette.

#### `Rect::OffsetMin`

```cpp
constexpr Rect OffsetMin(const Vec2& d) const { return {min.x + d.x, min.y + d.y, max.x, max.y}; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `d` | `const Vec2&` | | Delta applied to `min` only. |

**Returns:** a rect whose `min` moved by `d` and whose `max` is unchanged, so
the rect resizes rather than translates.

A positive `d` moves the top-left inward and shrinks the rect. A negative `d`
grows it.

#### `Rect::OffsetMax`

```cpp
constexpr Rect OffsetMax(const Vec2& d) const { return {min.x, min.y, max.x - d.x, max.y - d.y}; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `d` | `const Vec2&` | | Delta subtracted from `max`. |

**Returns:** a rect whose `max` moved by `-d` and whose `min` is unchanged.

The sign is inverted relative to `OffsetMin` so that a positive `d` shrinks from
that edge in both cases.

#### `Rect::Translate`

```cpp
constexpr Rect Translate(const Vec2& d) const { return {min.x + d.x, min.y + d.y, max.x + d.x, max.y + d.y}; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `d` | `const Vec2&` | | Delta in pixels. |

**Returns:** the same rect moved by `d`, size unchanged.

### Predicates

#### `Rect::IsEmpty`

```cpp
constexpr bool IsEmpty() const { return max.x <= min.x || max.y <= min.y; }
```

**Returns:** true when the rect encloses no area.

A zero-width or zero-height rect is empty. An inverted rect is empty too, which
makes this safe to call on the output of an untrusted intersection. Every
primitive in [`DrawList`](draw-list.md) checks this and emits nothing when it
is true.

#### `Rect::Contains`

```cpp
constexpr bool Contains(const Vec2& p) const {
constexpr bool Contains(const Rect& r) const {
```

Two overloads.

| Overload | Parameters | Returns |
|---|---|---|
| Point | `p`: `const Vec2&` | True when `p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y`. |
| Rect | `r`: `const Rect&` | True when `r` lies entirely within this rect. |

**Returns:** whether the argument lies inside.

The point overload is half-open, so the right and bottom edges are excluded and
adjacent cells never both claim a point. The rect overload is inclusive on both
sides.

#### `Rect::Overlaps`

```cpp
constexpr bool Overlaps(const Rect& r) const {
    return min.x < r.max.x && max.x > r.min.x && min.y < r.max.y && max.y > r.min.y;
}
```

**Returns:** true when the two rects share any area.

Edge contact does not count as overlap. Two rects that merely touch along a
border return false.

#### `Rect::Intersect`

```cpp
constexpr Rect Intersect(const Rect& r) const {
    return {Min(min.x, r.min.x), Min(min.y, r.min.y), Max(max.x, r.max.x), Max(max.y, r.max.y)};
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Rect to combine with. |

**Returns:** a rect covering both inputs.

This is a union, not an intersection, and the name is misleading. It takes the
smaller of the two `min` values and the larger of the two `max` values, so the
result always contains both operands and can never come out inverted, even when
one input is degenerate or inverted. That property is what makes it safe to use
in layout code where a rect may have collapsed to nothing.

**For clipping use [`IntersectStrict`](#intersectstrict) instead.** A union used
as a scissor rect would never reject anything.

#### `Rect::ClampPoint`

```cpp
constexpr Vec2 ClampPoint(const Vec2& p) const {
    return {Clamp(p.x, min.x, max.x), Clamp(p.y, min.y, max.y)};
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `p` | `const Vec2&` | | Point in any coordinate space. |

**Returns:** the point pulled inside the rect, component-wise.

Clamps to the inclusive range, so a point on the far edge stays where it is.
This is what keeps a slider thumb or a dragged window inside its track.

### Comparison

```cpp
constexpr bool operator==(const Rect& r) const { return min == r.min && max == r.max; }
constexpr bool operator!=(const Rect& r) const { return !(*this == r); }
```

Exact comparison of both corners, with no tolerance. The draw list relies on it to
decide whether two batches share a clip rect, which is a correctness decision:
if two rects are one float apart they really are two scissors.

## `IntersectStrict`

```cpp
inline Rect IntersectStrict(const Rect& a, const Rect& b) {
```

Free function. The real intersection, and the one clipping code uses.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `const Rect&` | | First rect. |
| `b` | `const Rect&` | | Second rect. |

**Returns:** the shared area, or an empty rect when the two do not overlap.

On overlap it returns `Rect(l, t, r, bm)` with `l = Max(a.min.x, b.min.x)`,
`t = Max(a.min.y, b.min.y)`, `r = Min(a.max.x, b.max.x)` and
`bm = Min(a.max.y, b.max.y)`. When `r <= l || bm <= t` it returns
`Rect(l, t, l, bm)`, a zero-width rect at the top-left of where the overlap
would have been. The result is never inverted, so `IsEmpty()` and `Area() <= 0`
are both valid tests on it.

{% hint style="warning" %}
`Rect::Intersect` and `IntersectStrict` are not interchangeable despite the
similar names. `Intersect` unions, `IntersectStrict` intersects. Reaching for
`Rect::Intersect` when you meant to clip produces a scissor rect that covers
everything and silently disables clipping for that subtree.
{% endhint %}

**Example**

```cpp
// Scissor a child against its parent before pushing the clip.
const Rect visible = IntersectStrict(child, parent.content);
if (visible.IsEmpty()) return;   // nothing to draw, skip the whole subtree
const Rect previous = dl.PushClip(visible);
// ... draw children ...
dl.PopClip(previous);
```

## `CornerRadius`

```cpp
struct CornerRadius {
    f32 tl = 0.0f;
    f32 tr = 0.0f;
    f32 br = 0.0f;
    f32 bl = 0.0f;
};
```

Per-corner radii in pixels, listed clockwise from the top left. Storing them
separately rather than as a single scalar is what lets a window have small
rounded top corners and square bottom corners, which is what a docked panel or a
card that meets the viewport edge wants.

| Constructor | `constexpr` | Meaning |
|---|---|---|
| `CornerRadius()` | yes | All zero, a square-cornered box. |
| `CornerRadius(v)` | yes | Same radius on all four corners. |
| `CornerRadius(a, b, c, d)` | yes | `tl`, `tr`, `br`, `bl` individually. |

### `CornerRadius::Diagonal`

```cpp
static constexpr CornerRadius Diagonal(f32 v) { return {v, 0.0f, v, 0.0f}; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `v` | `f32` | | Radius for the two diagonal corners. |

**Returns:** a radius with only `tl` and `br` set.

This is the shape reference UIs use for cards and accordions.

### `CornerRadius::ClampTo`

```cpp
CornerRadius ClampTo(const Rect& r) const {
```

Not `constexpr`: it is a plain inline member, so calling it with compile-time
arguments still runs at runtime.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Rect the radii must fit inside. |

**Returns:** a copy with every radius limited to what `r` can accommodate.

Each radius is first capped at the rect's width and height, then to
`[0, Height * 0.5]`, then the two radii on the top edge and the two on the
bottom edge are scaled down together if their sum exceeds the width. That
covers both ways a radius can be too large: a single corner larger than the box,
and two adjacent corners whose arcs would meet before reaching the edge.

{% hint style="warning" %}
Without this clamp the signed distance field folds over itself. The rounded-box
SDF subtracts the radius from the half-extent, so a radius larger than the
half-extent makes that term negative, and the interior distance the shader
integrates comes out with the wrong sign. The result is a shape with a dark
seam or an inverted band through it, usually worst at the center of the box
rather than at the corners, which makes it hard to trace back to the cause.
{% endhint %}

[`DrawList::AddBox`](draw-list.md) calls this on every quad against the core box
after the glow spread has been removed, which also keeps the glow band an even
width all the way around.

### `CornerRadius::MaxComponent`

```cpp
f32 MaxComponent() const { return Max(Max(tl, tr), Max(br, bl)); }
```

Not `constexpr`.

**Returns:** the largest of the four radii.

Useful as a cheap bound on how far a shape extends past its bounding box, for
example when padding a layout rect for a glow.

## See also

- [Core types](core-types.md) - `Vec2`, `Min`, `Max`, `Clamp` and the rest of the helpers these are built from.
- [Draw list](draw-list.md) - the primitives that consume rects and radii.
- [Colour](color.md) - the other half of a `AddBox` call.