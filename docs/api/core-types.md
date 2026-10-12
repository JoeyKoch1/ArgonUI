# Core types

Scalar aliases, the two small vector types and the free math helpers that the rest
of ArgonUI is written against. Everything here is `constexpr` where it can be, so
layout code runs at compile time. You need this page when you write a theme, a
custom widget, or any code that computes sizes and positions by hand.

Source: [`include/argonui/Core/Types.h`](../../include/argonui/Core/Types.h).

## Scalar aliases

Fixed-width integers and IEEE floats with short names. `f32` is `float` and
`f64` is `double`, so a `f32` literal needs no `f` suffix to become an `f32`, but
writing the suffix keeps mixed expressions from silently promoting to `double`.

| Alias | Underlying type | Bytes |
|---|---|---|
| `u8` | `std::uint8_t` | 1 |
| `u16` | `std::uint16_t` | 2 |
| `u32` | `std::uint32_t` | 4 |
| `u64` | `std::uint64_t` | 8 |
| `i8` | `std::int8_t` | 1 |
| `i16` | `std::int16_t` | 2 |
| `i32` | `std::int32_t` | 4 |
| `i64` | `std::int64_t` | 8 |
| `f32` | `float` | 4 |
| `f64` | `double` | 8 |

Pixel coordinates, sizes, radii and times are all `f32`. Sizes and counts that
cross into the backend are `u32`, because the GPU-facing structures in
[`Vertex`](vertex.md) and [`DrawList`](draw-list.md) are fixed width.

## Constants

### `kPi`

```cpp
inline constexpr f32 kPi = 3.14159265358979323846f;
```

**Returns:** nothing, it is a `constexpr` value of `pi` narrowed to `f32`.

Angles are radians everywhere in ArgonUI. The one exception is
[`DrawList::AddArc`](draw-list.md), which takes and returns degrees because that
is how gauge angles are read off a design.

### `kEpsilon`

```cpp
inline constexpr f32 kEpsilon = 1e-6f;
```

**Returns:** nothing, it is a `constexpr` tolerance for length comparisons.

Use it where a geometric quantity can legitimately reach zero through
cancellation, such as the length of a difference vector. Use
[`NearlyEqual`](#nearlyequal) for comparing two values that are both supposed to
be meaningful.

## `Vec2`

```cpp
struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 vx, f32 vy) : x(vx), y(vy) {}

    // operators omitted, see the table below
};
```

A two-component position or size in pixels. Both fields default to zero, so
`Vec2{}` is the origin and `Vec2{8.0f}`-style single-argument construction does
not exist. Y grows downward, matching screen space and the framebuffer.

| Constructor | Meaning |
|---|---|
| `Vec2()` | Zero vector. |
| `Vec2(f32 vx, f32 vy)` | Explicit components. |

Operators, all `constexpr` except the compound assignments:

| Expression | Result |
|---|---|
| `a + b`, `a - b` | Component-wise sum and difference. |
| `a * s` | Component-wise scale. Also available as `s * a` via the free operator. |
| `a * b` | Component-wise product. Handy for scaling a size by a per-axis factor. |
| `a / s` | Component-wise divide. |
| `-a` | Negation. |
| `a += b`, `a -= b`, `a *= s` | In-place, return `*this`. |
| `a == b`, `a != b` | Exact component-wise comparison. |

{% hint style="warning" %}
`==` is an exact float comparison with no tolerance. Two rect corners computed by
different routes will differ in the last bit often enough to matter. Compare
components with [`NearlyEqual`](#nearlyequal) when the values come from
arithmetic rather than from literals.
{% endhint %}

### `operator*(f32 s, const Vec2& v)`

```cpp
inline constexpr Vec2 operator*(f32 s, const Vec2& v) { return {v.x * s, v.y * s}; }
```

Scalar-on-the-left form, so `0.5f * size` compiles without a cast.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `s` | `f32` | | Scale factor. |
| `v` | `const Vec2&` | | Vector to scale. |

**Returns:** the scaled `Vec2`.

## `Vec4`

```cpp
struct Vec4 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 vx, f32 vy, f32 vz, f32 vw) : x(vx), y(vy), z(vz), w(vw) {}
    explicit constexpr Vec4(const Vec2& xy, f32 vz = 0.0f, f32 vw = 0.0f) : x(xy.x), y(xy.y), z(vz), w(vw) {}

    constexpr Vec2 xy() const { return {x, y}; }
};
```

Four components, used where a color or a 2D vector needs a uniform slot. ArgonUI
does not otherwise model 3D; `z` and `w` exist to match what the backends expect
from a `vec4` attribute and to keep `Vec2` convertible without a helper struct.

| Constructor | Meaning |
|---|---|
| `Vec4()` | All zero, including `w`. |
| `Vec4(f32 vx, f32 vy, f32 vz, f32 vw)` | Explicit components. |
| `Vec4(const Vec2& xy, f32 vz, f32 vw)` | `explicit`, so a `Vec2` never converts implicitly. Defaults `vz` and `vw` to 0. |

### `Vec4::xy()`

```cpp
constexpr Vec2 xy() const { return {x, y}; }
```

**Returns:** the first two components as a `Vec2`.

## Scalar helpers

Free functions in `argon::ui`, usable with any arithmetic type.

### `Min`

```cpp
template <typename T>
inline constexpr T Min(T a, T b) { return a < b ? a : b; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `T` | | First operand. |
| `b` | `T` | | Second operand. |

**Returns:** the smaller of the two. On a tie, `a`.

### `Max`

```cpp
template <typename T>
inline constexpr T Max(T a, T b) { return a > b ? a : b; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `T` | | First operand. |
| `b` | `T` | | Second operand. |

**Returns:** the larger of the two. On a tie, `a`.

### `Clamp`

```cpp
template <typename T>
inline constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `v` | `T` | | Value to constrain. |
| `lo` | `T` | | Lower bound, inclusive. |
| `hi` | `T` | | Upper bound, inclusive. |

**Returns:** `v` when it lies in `[lo, hi]`, otherwise the nearer bound.

There is no guard for `lo > hi`. A caller that mixes up the bounds gets `hi`
for every input, which reads as a layout that suddenly collapses.

### `Clamp01`

```cpp
inline f32 Clamp01(f32 v) { return Clamp(v, 0.0f, 1.0f); }
```

**Returns:** `v` clamped to `[0, 1]`.

The convenience form for color components and normalized texture coordinates.
ArgonUI clamps on read rather than on write, so a `Color` may hold values outside
`[0, 1]` after [`Color::Scaled`](color.md) and they are clamped at upload.

### `Lerp`

```cpp
template <typename T>
inline constexpr T Lerp(T a, T b, f32 t) { return static_cast<T>(a + (b - a) * t); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `T` | | Value at `t == 0`. |
| `b` | `T` | | Value at `t == 1`. |
| `t` | `f32` | | Blend factor, normally in `[0, 1]`. |

**Returns:** the linear blend of `a` and `b`.

`t` is `f32` regardless of `T`, and the result is cast back to `T`. Passing `t`
outside `[0, 1]` extrapolates rather than clamps, which is what an overshooting
easing curve wants.

### `DampFactor`

```cpp
inline f32 DampFactor(f32 dt, f32 rate) {
    return 1.0f - std::exp(-rate * dt);
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `dt` | `f32` | | Seconds since the previous frame. |
| `rate` | `f32` | | Fraction of the remaining gap closed per second. |

**Returns:** the fraction to move this frame, in `(0, 1)` for positive `dt`.

The curve closes a fixed fraction of the distance per second rather than a fixed
number of pixels, so an animation covers the same fraction of its travel in the
same wall-clock time at any frame rate. Roughly `rate == 12` reads as snappy,
`rate == 4` as lazy.

### `Damp`

```cpp
inline f32 Damp(f32 current, f32 target, f32 dt, f32 rate) {
    return Lerp(current, target, DampFactor(dt, rate));
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `current` | `f32` | | Value at the start of the frame. |
| `target` | `f32` | | Value being animated towards. |
| `dt` | `f32` | | Seconds since the previous frame. |
| `rate` | `f32` | | Fraction of the remaining gap closed per second. |

**Returns:** the smoothed value for this frame.

**Example**

```cpp
// Hover highlight: same feel at 30 fps and at 240 fps.
f32 hover = Damp(hover, isHovered ? 1.0f : 0.0f, ctx.DeltaTime(), 14.0f);
```

### `Sign`

```cpp
inline f32 Sign(f32 v) { return v < 0.0f ? -1.0f : 1.0f; }
```

**Returns:** `-1.0f` when `v` is negative, `1.0f` otherwise.

{% hint style="warning" %}
`Sign(0.0f)` is `1.0f`, and `Sign(-0.0f)` is `1.0f`. For a value that can sit at
exactly zero at rest, a sign flip will jitter between `-1` and `+1` around that
point.
{% endhint %}

### `NearlyEqual`

```cpp
inline bool NearlyEqual(f32 a, f32 b, f32 eps = 0.001f) { return std::fabs(a - b) <= eps; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `f32` | | First value. |
| `b` | `f32` | | Second value. |
| `eps` | `f32` | `0.001f` | Absolute tolerance. |

**Returns:** true when the two values are within `eps` of each other.

The tolerance is absolute, not relative, so it means something different at 1.0
than at 10000.0. Use a smaller `eps` for angles, where one degree is
`0.01745` radians.

**Example**

```cpp
// Skip the trigonometry when the rotation is visually zero.
if (NearlyEqual(angleRadians, 0.0f, 1e-4f) == false) { /* rotate */ }
```

## See also

- [Geometry](geometry.md) - `Rect` and `CornerRadius`, the shapes these vectors describe.
- [Colour](color.md) - component clamping and blending built on these helpers.
- [Vertex format](vertex.md) - where `f32` and `u32` meet the GPU.
- [Draw list](draw-list.md) - the primitives these types feed.