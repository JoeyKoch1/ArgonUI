// ArgonUI - Core/Rect.h
// Axis-aligned rectangle plus the 4-corner radius description used by ArgonUI's
// rounded-box shader. Every drawable in ArgonUI is "a rounded box with a fill,
// an optional inner border and an optional outer glow", so CornerRadius is a
// first-class concept rather than an afterthought.
#pragma once

#include "Types.h"

namespace argon::ui {

// ---------------------------------------------------------------------------
// Rect (min/max form - matches what most GPU pipelines want)
// ---------------------------------------------------------------------------
struct Rect {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};

    constexpr Rect() = default;
    constexpr Rect(f32 l, f32 t, f32 r, f32 b) : min(l, t), max(r, b) {}
    constexpr Rect(const Vec2& mn, const Vec2& mx) : min(mn), max(mx) {}
    explicit Rect(const Vec2& size) : min(0.0f, 0.0f), max(size.x, size.y) {}

    constexpr f32 Width() const { return max.x - min.x; }
    constexpr f32 Height() const { return max.y - min.y; }
    constexpr Vec2 Size() const { return {max.x - min.x, max.y - min.y}; }
    constexpr f32 Area() const { return Width() * Height(); }

    constexpr Vec2 GetCenter() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    constexpr Vec2 GetTopLeft() const { return min; }
    constexpr Vec2 GetTopRight() const { return {max.x, min.y}; }
    constexpr Vec2 GetBottomRight() const { return max; }
    constexpr Vec2 GetBottomLeft() const { return {min.x, max.y}; }

    constexpr Rect Left(f32 w) const { return {min.x, min.y, min.x + w, max.y}; }
    constexpr Rect Right(f32 w) const { return {max.x - w, min.y, max.x, max.y}; }
    constexpr Rect Top(f32 h) const { return {min.x, min.y, max.x, min.y + h}; }
    constexpr Rect Bottom(f32 h) const { return {min.x, max.y - h, max.x, max.y}; }
    constexpr Rect TopLeft(const Vec2& sz) const { return {min.x, min.y, min.x + sz.x, min.y + sz.y}; }
    constexpr Rect TopRight(const Vec2& sz) const { return {max.x - sz.x, min.y, max.x, min.y + sz.y}; }
    constexpr Rect BottomRight(const Vec2& sz) const { return {max.x - sz.x, max.y - sz.y, max.x, max.y}; }
    constexpr Rect BottomLeft(const Vec2& sz) const { return {min.x, max.y - sz.y, min.x + sz.x, max.y}; }

    /// Shrink/expand on all sides.
    constexpr Rect Expand(f32 amount) const { return {min.x - amount, min.y - amount, max.x + amount, max.y + amount}; }
    /// Shrink (positive) / grow (negative) on all sides.
    constexpr Rect Shrink(f32 amount) const { return Expand(-amount); }
    /// Shrink only the left/top by (all, y).
    constexpr Rect OffsetMin(const Vec2& d) const { return {min.x + d.x, min.y + d.y, max.x, max.y}; }
    /// Shrink only the right/bottom by (all, y).
    constexpr Rect OffsetMax(const Vec2& d) const { return {min.x, min.y, max.x - d.x, max.y - d.y}; }
    constexpr Rect Translate(const Vec2& d) const { return {min.x + d.x, min.y + d.y, max.x + d.x, max.y + d.y}; }

    constexpr bool IsEmpty() const { return max.x <= min.x || max.y <= min.y; }

    constexpr bool Contains(const Vec2& p) const {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }

    constexpr bool Contains(const Rect& r) const {
        return r.min.x >= min.x && r.max.x <= max.x && r.min.y >= min.y && r.max.y <= max.y;
    }

    constexpr bool Overlaps(const Rect& r) const {
        return min.x < r.max.x && max.x > r.min.x && min.y < r.max.y && max.y > r.min.y;
    }

    constexpr Rect Intersect(const Rect& r) const {
        return {Min(min.x, r.min.x), Min(min.y, r.min.y), Max(max.x, r.max.x), Max(max.y, r.max.y)};
    }

    /// Clamp a point so it stays inside this rect (used by sliders and drags).
    constexpr Vec2 ClampPoint(const Vec2& p) const {
        return {Clamp(p.x, min.x, max.x), Clamp(p.y, min.y, max.y)};
    }

    constexpr bool operator==(const Rect& r) const { return min == r.min && max == r.max; }
    constexpr bool operator!=(const Rect& r) const { return !(*this == r); }
};

/// Intersection that returns an empty rect when the inputs do not overlap.
/// (Intersect above is intentionally forgiving for layout use - it never
/// produces an inverted rect - but clipping must use this stricter form.)
inline Rect IntersectStrict(const Rect& a, const Rect& b) {
    const f32 l = Max(a.min.x, b.min.x);
    const f32 t = Max(a.min.y, b.min.y);
    const f32 r = Min(a.max.x, b.max.x);
    const f32 bm = Min(a.max.y, b.max.y);
    if (r <= l || bm <= t) return Rect(l, t, l, bm);
    return Rect(l, t, r, bm);
}

// ---------------------------------------------------------------------------
// CornerRadius - clockwise from top-left.
// ---------------------------------------------------------------------------
struct CornerRadius {
    f32 tl = 0.0f;
    f32 tr = 0.0f;
    f32 br = 0.0f;
    f32 bl = 0.0f;

    constexpr CornerRadius() = default;
    constexpr CornerRadius(f32 v) : tl(v), tr(v), br(v), bl(v) {}
    constexpr CornerRadius(f32 a, f32 b, f32 c, f32 d) : tl(a), tr(b), br(c), bl(d) {}

    /// Circular sides: TL + BR only. (Ref UIs use this for cards/accordions.)
    static constexpr CornerRadius Diagonal(f32 v) { return {v, 0.0f, v, 0.0f}; }

    /// Returns a copy whose radii never exceed the rect's own half extents,
    /// so the SDF can never fold over itself.
    CornerRadius ClampTo(const Rect& r) const {
        const f32 hx = r.Width() * 0.5f;
        const f32 hy = r.Height() * 0.5f;
        // A single corner may eat at most half of each axis; adjacent corners
        // may not together exceed the full edge length.
        const f32 limY = Min(hy, r.Height());
        const f32 limX = Min(hx, r.Width());
        f32 ntl = Clamp(Min(tl, limX), 0.0f, limY);
        f32 ntr = Clamp(Min(tr, limX), 0.0f, limY);
        f32 nbr = Clamp(Min(br, limX), 0.0f, limY);
        f32 nbl = Clamp(Min(bl, limX), 0.0f, limY);
        const f32 topSum = ntl + ntr;
        if (topSum > limX) { const f32 k = limX / topSum; ntl *= k; ntr *= k; }
        const f32 botSum = nbl + nbr;
        if (botSum > limX) { const f32 k = limX / botSum; nbl *= k; nbr *= k; }
        return {ntl, ntr, nbr, nbl};
    }

    f32 MaxComponent() const { return Max(Max(tl, tr), Max(br, bl)); }
};

} // namespace argon::ui