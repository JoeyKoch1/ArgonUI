# Colour

`Color` holds four normalized float channels and knows how to pack itself into
the byte order the GPU expects. Read this page when you are defining a theme,
tinting an image, or reaching past the built-in style colours.

Source: [`include/argonui/Core/Color.h`](../../include/argonui/Core/Color.h).

## `Color`

```cpp
struct Color {
    f32 r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(f32 cr, f32 cg, f32 cb, f32 ca = 1.0f) : r(cr), g(cg), b(cb), a(ca) {}

    // methods omitted, see below
};
```

Four channels in `[0, 1]` where `0.0f` is zero and `1.0f` is full. The default
constructor is opaque black. Alpha is the only channel with a default, because
black-with-full-alpha is the one value a caller cannot reasonably be expected to
spell out every time.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `cr` | `f32` | | Red. |
| `cg` | `f32` | | Green. |
| `cb` | `f32` | | Blue. |
| `ca` | `f32` | `1.0f` | Alpha. |

{% hint style="info" %}
Channels are not clamped on assignment. A value outside `[0, 1]` survives until
it is packed, where each component is clamped to `[0, 1]` on the way out. This
keeps intermediate arithmetic cheap and means [`Color::Scaled`](#colorscaled)
can overshoot without a branch.
{% endhint %}

### Comparison

```cpp
constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
constexpr bool operator!=(const Color& o) const { return !(*this == o); }
```

Exact, component-wise, with no tolerance.

## Construction from integers

### `Color::Hex`

```cpp
static constexpr Color Hex(u32 rgba, f32 alphaScale = 1.0f) {
```

Static, `constexpr`. Builds a color from a packed literal.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `rgba` | `u32` | | `0xRRGGBBAA`, or `0xRRGGBB` with an implied alpha of 0. |
| `alphaScale` | `f32` | `1.0f` | Multiplies the alpha byte. |

**Returns:** the color with each byte divided by 255.

A six-digit literal such as `0x1E90FF` is opaque black, because the missing
low byte is read as an alpha of 0. Write the alpha out: `0x1E90FFFF` is the
opaque Dodger blue.

`alphaScale` exists so one literal can express a family of states, for example
the same hover colour at full and half opacity.

**Example**

```cpp
const Color accent = Color::Hex(0x3B82F6FF);              // opaque blue
const Color faint  = Color::Hex(0x3B82F6FF, 0.35f);       // same hue, 35% alpha
```

### `Color::FromABGR`

```cpp
static constexpr Color FromABGR(u32 abgr, f32 alphaScale = 1.0f) {
```

Static, `constexpr`. Reads the byte order that Win32 `COLORREF` and several
native UI toolkits use.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `abgr` | `u32` | | `0xAABBGGRR`, so red is in the low byte. |
| `alphaScale` | `f32` | `1.0f` | Multiplies the alpha byte. |

**Returns:** the color with each byte divided by 255.

This is the inverse of [`Color::ToU32`](#colortou32). Use it when the value came
out of a Win32 API; use [`Color::Hex`](#colorhex) for everything else, because
`0xRRGGBB` is what a designer sends.

## Packing

### `Color::ToU32`

```cpp
u32 ToU32() const {
```

**Returns:** the color as `0xAABBGGRR`, rounded and clamped per channel.

Each channel is clamped to `[0, 1]`, scaled by 255 and rounded with `+0.5f`
before truncation. Alpha sits in the high byte.

### `Color::ToVertex`

```cpp
u32 ToVertex() const {
```

**Returns:** the color as a `u32` whose **memory** layout is `R,G,B,A`, red in
the low byte.

Same clamping and rounding as `ToU32`, but the byte order is the opposite.

{% hint style="success" %}
This packing is the reason a vertex buffer upload is a plain `memcpy`. A
`Vertex::fill` written with `ToVertex()` is already laid out as `DXGI_FORMAT_R8G8B8A8_UNORM`,
`VK_FORMAT_R8G8B8A8_UNORM` and `GL_RGBA8`, so all four backends read the same
bytes as colour with no swizzle and no per-vertex conversion pass.
{% endhint %}

Use `ToVertex()` for anything that goes into a [`Vertex`](vertex.md), and
`ToU32()` when you are matching an external integer colour convention. Passing
`ToU32()` to the vertex stream swaps red and blue on every backend at once,
which reads as an entire theme being slightly wrong rather than as a clear bug.

## Modification

None of these mutate. Each returns a new `Color`, which keeps a style entry
usable as a constant.

### `Color::WithAlpha`

```cpp
Color WithAlpha(f32 na) const { return Color(r, g, b, na); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `na` | `f32` | | Replacement alpha. |

**Returns:** the same RGB with a new alpha.

### `Color::Scaled`

```cpp
Color Scaled(f32 s) const { return Color(r * s, g * s, b * s, a); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `s` | `f32` | | Multiplier applied to RGB only. |

**Returns:** the color with RGB multiplied by `s`, alpha untouched.

Scaling RGB alone brightens toward black rather than fading toward transparent.
For a hover state that should look like the same colour at lower opacity, use
`WithAlpha`.

### `Color::Tinted`

```cpp
Color Tinted(const Color& o) const { return Color(r * o.r, g * o.g, b * o.b, a * o.a); }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `o` | `const Color&` | | Color to multiply by. |

**Returns:** the component-wise product.

White is the identity, so `Tinted(Color(1,1,1,1))` returns the receiver. This
is what image and text tinting means in ArgonUI: the tint multiplies both the
RGB and the alpha of whatever the shader samples.

### `operator*(const Color&, f32)`

```cpp
inline Color operator*(const Color& c, f32 s) { return c.Scaled(s); }
```

Free function, equivalent to `Color::Scaled`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `c` | `const Color&` | | Color to scale. |
| `s` | `f32` | | Multiplier for RGB. |

**Returns:** `c.Scaled(s)`.

## Generation and blending

### `Color::LerpC`

```cpp
static Color LerpC(const Color& a, const Color& b, f32 t) {
```

Static.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `const Color&` | | Color at `t == 0`. |
| `b` | `const Color&` | | Color at `t == 1`. |
| `t` | `f32` | | Blend factor, normally in `[0, 1]`. |

**Returns:** the linear blend of all four channels.

Named with a `C` suffix because plain `Lerp` from
[`Core/Types.h`](core-types.md) takes numeric types and would not compile on
`Color`. Interpolating in this space crosses through muddier midpoints than
HSV does, so prefer [`Color::HSV`](#colorhsv) for a hue sweep and `LerpC` for a
fade between two states of one hue.

### `Color::HSV`

```cpp
static Color HSV(f32 h, f32 s, f32 v, f32 a = 1.0f) {
```

Static.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `h` | `f32` | | Hue, in turns. `0.0` is red, `1.0` wraps back to red. |
| `s` | `f32` | | Saturation, `[0, 1]`. |
| `v` | `f32` | | Value, `[0, 1]`. |
| `a` | `f32` | `1.0f` | Alpha. |

**Returns:** the equivalent RGB color.

`h` is normalized by turn rather than by degrees, so it wraps with
`std::fmod`: `1.25f` and `0.25f` are the same hue, and negative values wrap
forward. The hue is quantized to one of six sectors, so hue interpolation steps
rather than sweeps continuously.

### `Color::Luminance`

```cpp
f32 Luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
```

**Returns:** the Rec. 709 relative luminance, ignoring alpha.

The result is unweighted by alpha, so a fully transparent color still reports the
luminance of its RGB. A translucent overlay drawn on a dark background needs
either `Luminance() * a` or a blend against the known backdrop, otherwise
contrast checks will read it as brighter than it appears.

## See also

- [Core types](core-types.md) - `Clamp01` and `Lerp`, which the packing and blending use.
- [Vertex format](vertex.md) - the `u32` colour fields this packs into.
- [Draw list](draw-list.md) - where a `Color` becomes pixels.