// ArgonUI - Draw/Vertex.h
//
// THE core idea of ArgonUI.
//
// Every drawable primitive - rounded panel, card, toggle track, toggle knob,
// slider rail, slider thumb, pill button, icon, glyph, thumbnail, glow, divider,
// arc - is expressed as a single quad carrying a *rounded-box signed distance
// field* description. One vertex format, one pipeline, one fragment shader per
// graphics API. Nothing is tessellated, nothing is CPU-rasterised.
//
// Consequences vs. an immediate-mode GUI built on plain triangles:
//   * Per-corner radii, exact at any DPI, one draw call.
//   * A filled+bordered card is ONE primitive, not two stacked rects.
//   * Soft outer glow / drop shadow is ONE primitive with no blur pass.
//   * Anti-aliasing is analytic (fwidth on the SDF), so it is correct under
//     arbitrary scaling and on high-DPI displays.
//
// Vertex layout is 64 bytes and identical for D3D11 / D3D12 / Vulkan / GL.
// Only the *interpretation* of the flags word differs per API, and that
// difference is a #define away in the shader.
#pragma once

#include "../Core/Color.h"
#include "../Core/Types.h"

namespace argon::ui {
/// Bit flags stored in Vertex::flags.
enum VertexFlags : u32 {
    /// No gradient: colour is the border/fill weighted mix.
    VF_FLAT = 0u,
    /// Linear gradient along the box's vertical axis (fill -> border colour).
    VF_GRADIENT_V = 1u << 0,
    /// Linear gradient along the box's horizontal axis.
    VF_GRADIENT_H = 1u << 1,
    /// Radial gradient from centre outward.
    VF_GRADIENT_RADIAL = 1u << 2,
    VF_GRADIENT_MASK = VF_GRADIENT_V | VF_GRADIENT_H | VF_GRADIENT_RADIAL,

    /// Inset shadow: darkens inside the box instead of glowing outside it.
    /// (Used for pressed buttons and text-input wells.)
    VF_INSET_SHADOW = 1u << 3,

    /// The bound texture is single-channel coverage (ArgonUI's font atlases).
    /// The shader reads coverage from .r and forces the tint to white, which
    /// keeps glyph atlaves R8 instead of RGBA8, a quarter of the memory for
    /// what, on a text-heavy menu, is the bulk of ArgonUI's VRAM.
    VF_TEX_COVERAGE = 1u << 4,
};

/// 64-byte vertex. Field order is chosen so every attribute is naturally
/// 4-byte aligned and the block is a multiple of 16 bytes for all four APIs.
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
static_assert(sizeof(Vertex) == 64, "ArgonUI vertex layout must be exactly 64 bytes");
static_assert(offsetof(Vertex, x) == 0, "");
static_assert(offsetof(Vertex, u) == 8, "");
static_assert(offsetof(Vertex, hx) == 16, "");
static_assert(offsetof(Vertex, rtl) == 24, "");
static_assert(offsetof(Vertex, borderWidth) == 40, "");
static_assert(offsetof(Vertex, glowWidth) == 44, "");
static_assert(offsetof(Vertex, fill) == 48, "");
static_assert(offsetof(Vertex, flags) == 60, "");

/// Index-count for one quad.
inline constexpr u32 kIndicesPerQuad = 6;

} // namespace argon::ui
