// ArgonUI - Core/Color.h
//
// Colour is stored as a packed u32 whose *memory* layout is R,G,B,A so that a
// vertex buffer memcpy is byte-compatible with DXGI_FORMAT_R8G8B8A8_UNORM,
// VK_FORMAT_R8G8B8A8_UNORM and GL_RGBA8. No backend needs a conversion pass.
#pragma once

#include "Types.h"

namespace argon::ui {
struct Color {
    f32 r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(f32 cr, f32 cg, f32 cb, f32 ca = 1.0f) : r(cr), g(cg), b(cb), a(ca) {}

    /// Hex literal helper: 0xRRGGBBAA or 0xRRGGBB.
    static constexpr Color Hex(u32 rgba, f32 alphaScale = 1.0f) {
        return Color(
            static_cast<f32>((rgba >> 24) & 0xFFu) / 255.0f,
            static_cast<f32>((rgba >> 16) & 0xFFu) / 255.0f,
            static_cast<f32>((rgba >> 8) & 0xFFu) / 255.0f,
            static_cast<f32>(rgba & 0xFFu) / 255.0f * alphaScale);
    }

    /// 0xAABBGGRR source form -> normalised RGBA. For written constants only;
    /// this is not the byte order the vertex stream uses.
    static constexpr Color FromABGR(u32 abgr, f32 alphaScale = 1.0f) {
        return Color(
            static_cast<f32>(abgr & 0xFFu) / 255.0f,
            static_cast<f32>((abgr >> 8) & 0xFFu) / 255.0f,
            static_cast<f32>((abgr >> 16) & 0xFFu) / 255.0f,
            static_cast<f32>((abgr >> 24) & 0xFFu) / 255.0f * alphaScale);
    }

    /// Normalised RGBA -> packed u32, red in the low byte. Byte-identical to
    /// ToVertex; this spelling exists so written constants read naturally.
    u32 ToU32() const {
        auto q = [](f32 v) -> u32 {
            const f32 c = Clamp01(v);
            return static_cast<u32>(c * 255.0f + 0.5f);
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    /// Normalised RGBA -> memory-ordered packed u32 (R in the low byte).
    /// Stored in the vertex stream.
    u32 ToVertex() const {
        auto q = [](f32 v) -> u32 {
            const f32 c = Clamp01(v);
            return static_cast<u32>(c * 255.0f + 0.5f);
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    Color WithAlpha(f32 na) const { return Color(r, g, b, na); }
    Color Scaled(f32 s) const { return Color(r * s, g * s, b * s, a); }

    /// Multiply RGB, keep alpha (what "tint" means for images and text).
    Color Tinted(const Color& o) const { return Color(r * o.r, g * o.g, b * o.b, a * o.a); }

    static Color LerpC(const Color& a, const Color& b, f32 t) {
        return Color(Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t));
    }

    static Color HSV(f32 h, f32 s, f32 v, f32 a = 1.0f) {
        h = std::fmod(h, 1.0f);
        if (h < 0.0f) h += 1.0f;
        const f32 i = std::floor(h * 6.0f);
        const f32 f = h * 6.0f - i;
        const f32 p = v * (1.0f - s);
        const f32 q = v * (1.0f - f * s);
        const f32 t = v * (1.0f - (1.0f - f) * s);
        switch (static_cast<int>(i) % 6) {
        case 0: return Color(v, t, p, a);
        case 1: return Color(q, v, p, a);
        case 2: return Color(p, v, t, a);
        case 3: return Color(p, q, v, a);
        case 4: return Color(t, p, v, a);
        default: return Color(v, p, q, a);
        }
    }

    f32 Luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

inline Color operator*(const Color& c, f32 s) { return c.Scaled(s); }

} // namespace argon::ui
