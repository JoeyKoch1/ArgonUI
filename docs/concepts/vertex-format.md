# The vertex format

`argon::ui::Vertex` is 64 bytes and it is the hardest contract in the library.
Changing it means changing four backends and four shader sources in the same
commit.

## Layout

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | `f32` | `x` | Screen-space pixel X of this corner |
| 4 | `f32` | `y` | Screen-space pixel Y of this corner |
| 8 | `f32` | `u` | 0 to 1 across the glow-expanded box |
| 12 | `f32` | `v` | 0 to 1 down the glow-expanded box |
| 16 | `f32` | `hx` | Core box half extent, X |
| 20 | `f32` | `hy` | Core box half extent, Y |
| 24 | `f32` | `rtl` | Top-left corner radius |
| 28 | `f32` | `rtr` | Top-right corner radius |
| 32 | `f32` | `rbr` | Bottom-right corner radius |
| 36 | `f32` | `rbl` | Bottom-left corner radius |
| 40 | `f32` | `borderWidth` | Inner border thickness, 0 for none |
| 44 | `f32` | `glowWidth` | Outer glow spread, 0 for none |
| 48 | `u32` | `fill` | Packed RGBA fill or tint |
| 52 | `u32` | `border` | Packed RGBA border, or gradient far stop |
| 56 | `u32` | `glowColor` | Packed RGBA glow colour |
| 60 | `u32` | `flags` | `VertexFlags` bitmask |

`static_assert` in the header pins every offset and `sizeof`. If you change the
struct the compiler will catch you before the backends do, which is the point.

## Why these fields

The vertex describes a box in its own local frame. `hx`/`hy` give its size, the
four radii give its corner treatment, `borderWidth` and `glowWidth` give the two
outline passes, and `x`/`y` place the corners in screen space.

The fragment shader does not use screen position to find the shape. It uses `u`
and `v`, which are interpolated across the quad. That is what makes rotation
free: rotate the four corners and the local frame rotates with them, so the SDF
stays correct without the shader knowing rotation happened.

Colours are constant across a quad, so they are marked `nointerpolation` in HLSL
and `flat` in GLSL. All four corners carry identical values, so interpolating
them would be a no-op anyway, but saying so saves the interpolator work and
removes any seam risk.

## Colour packing

```cpp
u32 ToVertex() const;   // r | g<<8 | b<<16 | a<<24
```

The low byte is red, so the `u32`'s memory layout in the vertex buffer is
R, G, B, A in that order. A `memcpy` of the vertex array is then byte
compatible with `DXGI_FORMAT_R8G8B8A8_UNORM`, `VK_FORMAT_R8G8B8A8_UNORM` and
`GL_RGBA`. No backend needs a conversion pass, and no repacking on upload.

`Color::ToU32()` uses the opposite order (`0xAABBGGRR`) for human-readable hex
constants. Do not confuse the two. `ToVertex` is what goes in a vertex.

## Vertex flags

| Flag | Effect |
|---|---|
| `VF_FLAT` | No gradient, colour is the fill and border mix. |
| `VF_GRADIENT_V` | Linear gradient down the box. |
| `VF_GRADIENT_H` | Linear gradient across the box. |
| `VF_GRADIENT_RADIAL` | Radial gradient from the centre. |
| `VF_INSET_SHADOW` | Reserved for inset shadows. |
| `VF_TEX_COVERAGE` | Texture is single-channel coverage, read from `.r`. |

Gradients reuse the `border` word as the far stop, which is why a gradient with
no border still passes a colour there.

`VF_TEX_COVERAGE` is why font atlases can be R8. Without it the shader reads
coverage from `.a` and the texture's RGB from `.r`/`.g`/`.b`; with it, coverage
comes from `.r` and the tint is forced to white. Glyph atlaves are a quarter of
the VRAM an RGBA8 atlas would cost, and text is most of a menu.

## What the shader does

In order:

1. Rebuild local position from `u`, `v` and the expanded half extents.
2. Pick the radius for this quadrant from `radii`.
3. Evaluate the rounded-box SDF.
4. Derive coverage from `fwidth` of that distance, which gives analytic AA at
   any scale.
5. Split coverage into fill and inner border.
6. Compute the glow falloff in box space, not euclidean, so the halo reaches zero
   exactly at the expanded quad edge and is not clipped at the corners.
7. Modulate by the texture, multiplying alpha by coverage.
8. Composite the glow behind the shape in premultiplied space.
9. Discard if the result is fully transparent.

## Changing it

If you change `Vertex`:

1. Update `static_assert` in `include/argonui/Draw/Vertex.h`.
2. Update the input layout in `backends/d3d11/D3D11Backend.cpp`.
3. Update the input layout in `backends/d3d12/D3D12Backend.cpp`.
4. Update `VkVertexInputAttributeDescription` in
   `backends/vulkan/VulkanBackend.cpp`.
5. Update `ConfigureVertexArray` in `backends/opengl/GLBackend.cpp`.
6. Update `VSIn` in `backends/common/ArgonHLSL.h`.
7. Update `VSIn` in `shaders/argonui.hlsl`.
8. Update both GLSL stages in the OpenGL backend.
9. Update both GLSL stages in the Vulkan backend.

A mismatch here does not fail to compile on the HLSL side, because both sides
are separately valid. It renders something that looks roughly right and is
subtly wrong everywhere. That class of bug is the reason this page exists.

## See also

- [The draw list](draw-list.md)
- [Vertex reference](../api/vertex.md)