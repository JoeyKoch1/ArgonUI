# Vertex format

`Vertex` is the single primitive ArgonUI submits. Every drawable in the library,
rounded panel, card, toggle track, slider rail, icon, glyph, arc, glow, is one
quad carrying a rounded-box signed distance field description. Read this page
before writing a backend or a custom primitive.

Source: [`include/argonui/Draw/Vertex.h`](../../include/argonui/Draw/Vertex.h).

## The idea

A plain immediate-mode GUI tessellates each widget out of triangles on the CPU.
ArgonUI does not. It emits a quad with a description of the shape, and the
fragment shader evaluates the shape analytically from the interpolated data.

What that buys:

- Per-corner radii exact at any DPI, in one draw call.
- A filled, bordered card is one primitive, not two stacked rects.
- A soft outer glow or drop shadow is one primitive with no blur pass.
- Anti-aliasing is analytic, using `fwidth` on the distance, so it stays correct
  under arbitrary scaling and on high-DPI displays.

There is no tessellation and no CPU rasterization anywhere in the library.

## `VertexFlags`

```cpp
enum VertexFlags : u32 {
    VF_FLAT = 0u,
    VF_GRADIENT_V = 1u << 0,
    VF_GRADIENT_H = 1u << 1,
    VF_GRADIENT_RADIAL = 1u << 2,
    VF_GRADIENT_MASK = VF_GRADIENT_V | VF_GRADIENT_H | VF_GRADIENT_RADIAL,
    VF_INSET_SHADOW = 1u << 3,
    VF_TEX_COVERAGE = 1u << 4,
};
```

Unscoped enumeration over `u32`, stored in `Vertex::flags`. Bits 0 to 2 select a
gradient, bit 3 an inset shadow, bit 4 the texture sampling mode. OR them
together; there is no struct wrapping so they pass straight into the
`vertexFlags` parameter of any [`DrawList`](draw-list.md) primitive.

| Flag | Value | Meaning |
|---|---|---|
| `VF_FLAT` | `0u` | No gradient. Colour is the fill weighted toward the border where they overlap. The default. |
| `VF_GRADIENT_V` | `1u << 0` | Linear gradient along the box's vertical axis, from `fill` to `border`. |
| `VF_GRADIENT_H` | `1u << 1` | Linear gradient along the horizontal axis. |
| `VF_GRADIENT_RADIAL` | `1u << 2` | Radial gradient from the centre outward, normalized against the half-extents. |
| `VF_GRADIENT_MASK` | `1u << 2 \| 1u << 1 \| 1u << 0` | All three gradient bits. Use it to test whether any gradient is set. |
| `VF_INSET_SHADOW` | `1u << 3` | Darkens inside the box rather than glowing outside it. Used for pressed buttons and text-input wells. |
| `VF_TEX_COVERAGE` | `1u << 4` | The bound texture is single-channel coverage, read from `.r`. |

{% hint style="info" %}
The gradient flags are mutually exclusive in the shader: vertical wins over
horizontal, horizontal wins over radial. OR several together and the first match
in that order is what gets drawn.
{% endhint %}

`VF_TEX_COVERAGE` is the reason font atlases stay `R8` instead of `RGBA8`, which
is a quarter of the memory for the one texture that dominates VRAM in a
text-heavy menu. When the bit is set the shader reads coverage from the texel's
red channel and forces the sampled RGB to white, so the tint supplies all the
colour.

{% hint style="warning" %}
The shader libraries declare their own copies of these bits (`ARGON_VF_*` in the
HLSL and GLSL sources) rather than including this header, since shader
compilers cannot parse C++ headers. The numbers must stay in step by hand. A
flag you add here does not reach a GPU until the matching `#define` is added to
`shaders/argonui.hlsl` and to each backend's shader source.
{% endhint %}

## `Vertex`

```cpp
struct Vertex {
    f32 x;      ///< Screen-space pixel X of this corner.
    f32 y;      ///< Screen-space pixel Y of this corner.
    f32 u;      ///< 0..1 across the (possibly glow-expanded) box.
    f32 v;      ///< 0..1 down  the (possibly glow-expanded) box.
    f32 hx;     ///< Core box half-extent, X, in pixels.
    f32 hy;     ///< Core box half-extent, Y, in pixels.
    f32 rtl;    ///< Top-left     corner radius, px.
    f32 rtr;    ///< Top-right    corner radius, px.
    f32 rbr;    ///< Bottom-right corner radius, px.
    f32 rbl;    ///< Bottom-left  corner radius, px.
    f32 borderWidth;  ///< Inner border thickness, px (0 = no border).
    f32 glowWidth;    ///< Outer glow spread, px (0 = no glow).
    u32 fill;     ///< Packed RGBA fill / tint colour.
    u32 border;   ///< Packed RGBA border colour, or gradient's far stop.
    u32 glowColor;///< Packed RGBA glow colour.
    u32 flags;    ///< VertexFlags.
};
```

Exactly 64 bytes. The field order puts every attribute on a naturally 4-byte
aligned boundary, and the total is a multiple of 16 so no backend needs padding
or a mismatched stride. The header enforces this with `static_assert`; the
layout is load-bearing across all four backends.

### Field layout

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | `f32` | `x` | Screen-space pixel X of this corner. |
| 4 | `f32` | `y` | Screen-space pixel Y of this corner. |
| 8 | `f32` | `u` | 0 to 1 across the box, including any glow expansion. |
| 12 | `f32` | `v` | 0 to 1 down the box, including any glow expansion. |
| 16 | `f32` | `hx` | Core box half-extent on X, in pixels. Excludes glow. |
| 20 | `f32` | `hy` | Core box half-extent on Y, in pixels. Excludes glow. |
| 24 | `f32` | `rtl` | Top-left corner radius, pixels. |
| 28 | `f32` | `rtr` | Top-right corner radius, pixels. |
| 32 | `f32` | `rbr` | Bottom-right corner radius, pixels. |
| 36 | `f32` | `rbl` | Bottom-left corner radius, pixels. |
| 40 | `f32` | `borderWidth` | Inner border thickness, pixels. 0 means no border. |
| 44 | `f32` | `glowWidth` | Outer glow spread, pixels. 0 means no glow. |
| 48 | `u32` | `fill` | Packed RGBA fill, or the image tint. |
| 52 | `u32` | `border` | Packed RGBA border, or the gradient's far stop. |
| 56 | `u32` | `glowColor` | Packed RGBA glow colour. |
| 60 | `u32` | `flags` | `VertexFlags`. |

`size` is 64.

The four `u32` colour fields come from [`Color::ToVertex`](color.md), which packs
so that the memory order is `R,G,B,A`. A `Vertex` array can be handed to
`D3D11_VERTEX_BUFFER_DESC`, `VkBuffer` or `glBufferData` and read as
`R8G8B8A8_UNORM` with no conversion.

### How the shader reads it

The four corners of a quad share `hx`, `hy`, the radii, the widths, the colours
and the flags, and differ only in `x`, `y`, `u` and `v`. Everything constant
across the quad is still per-vertex because the vertex format is fixed and that
keeps the pipeline description to nine attributes with no constant-buffer stage.

The fragment shader recovers local coordinates as `p = (uv * 2 - 1) * (half +
glow)`, evaluates the rounded-box distance, and takes anti-aliasing from
`fwidth` of that distance. Since screen Y grows downward, `p.y > 0` selects the
bottom corner radii.

Because `u` and `v` are normalized, the shader reconstructs the box's own frame
regardless of where the quad landed. That is what lets
[`DrawList::AddBoxRotated`](draw-list.md) rotate a quad by rotating only its four
corners: no extra vertex data and no extra cost.

### Backend attribute mapping

Every backend binds the same nine attributes from the same offsets. D3D11 and
D3D12 use semantic indices 0 to 7 with `POSITION` for `x,y`; Vulkan uses
locations 0 to 8 with `VK_FORMAT_R32G32_SFLOAT` through `VK_FORMAT_R32_UINT`; GL
uses `glVertexAttribPointer` for the float attributes and
`glVertexAttribIPointer` with `GL_UNSIGNED_INT` for the four integers.

{% hint style="warning" %}
The integer attributes must be fed as integers, never as floats. A float
`glVertexAttribPointer` on the `fill`, `border`, `glowColor` or `flags` attribute
reinterprets the packed bits as a float and every colour in the frame comes out
as noise. The GL backend has a comment to the same effect at its VAO setup.
{% endhint %}

## `kIndicesPerQuad`

```cpp
inline constexpr u32 kIndicesPerQuad = 6;
```

**Returns:** nothing, it is the number of indices one quad contributes.

Two triangles, wound `0,1,2` and `0,2,3` from the quad's base vertex. Every
batch's `indexCount` is a multiple of this unless it came from
[`DrawList::AddTriangle`](draw-list.md) or [`DrawList::AddArc`](draw-list.md),
which add 3 indices per triangle.

## See also

- [Draw list](draw-list.md) - the code that builds these vertices.
- [Colour](color.md) - how the three packed `u32` fields are produced.
- [Geometry](geometry.md) - where `hx`, `hy` and the radii come from.