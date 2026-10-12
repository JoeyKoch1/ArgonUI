#include "argonui/Draw/DrawList.h"

#include <algorithm>

namespace argon::ui {

namespace {
/// Effectively infinite scissor rect. Backends clamp it against the
/// framebuffer, so the CPU side never has to know the display size.
constexpr f32 kHugeExtent = 1.0e6f;

Rect FullClip() { return Rect(-kHugeExtent, -kHugeExtent, kHugeExtent, kHugeExtent); }

inline u32 PackColor(const Color& c) { return c.ToVertex(); }

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

DrawList::DrawList() {
    clip_ = FullClip();
    clips_.push_back(clip_);
    cmds_.reserve(256);
    vertices_.reserve(8192);
    indices_.reserve(12288);
}

void DrawList::Reset() {
    vertices_.clear();
    indices_.clear();
    cmds_.clear();
    clips_.clear();
    clip_ = FullClip();
    clips_.push_back(clip_);
    curTexture_ = 0;
    curFlags_ = static_cast<u8>(CmdFlags::WhiteTexture);
    curBlend_ = BlendMode::Alpha;
    batchOpen_ = false;
    lastClip_ = clip_;
    stats_ = DrawStats{};
    stats_.clipRegions = 1;
}

void DrawList::Reserve(size_t vertexCapacity, size_t indexCapacity, size_t cmdCapacity) {
    vertices_.reserve(vertexCapacity);
    indices_.reserve(indexCapacity);
    cmds_.reserve(cmdCapacity);
    clips_.reserve(64);
}

// ---------------------------------------------------------------------------
// Clipping
// ---------------------------------------------------------------------------

u32 DrawList::InternClip(const Rect& r) {
    for (size_t i = 0; i < clips_.size(); ++i) {
        if (clips_[i] == r) return static_cast<u32>(i);
    }
    clips_.push_back(r);
    stats_.clipRegions = static_cast<u32>(clips_.size());
    return static_cast<u32>(clips_.size() - 1);
}

Rect DrawList::PushClip(const Rect& rect) {
    const Rect previous = clip_;
    clip_ = IntersectStrict(clip_, rect);
    InternClip(clip_);
    batchOpen_ = false;
    return previous;
}

void DrawList::PopClip(const Rect& previousClip) {
    clip_ = previousClip;
    batchOpen_ = false;
}

// ---------------------------------------------------------------------------
// Batch state
// ---------------------------------------------------------------------------

void DrawList::SetTexture(u32 textureId, CmdFlags flags) {
    const u8 f = static_cast<u8>(flags);
    if (curTexture_ == textureId && curFlags_ == f) return;
    curTexture_ = textureId;
    curFlags_ = f;
    batchOpen_ = false;
}

void DrawList::SetBlend(BlendMode blend) {
    if (curBlend_ == blend) return;
    curBlend_ = blend;
    batchOpen_ = false;
}

DrawList::BatchState DrawList::SaveBatchState() const {
    return BatchState{curTexture_, curFlags_, curBlend_, clip_};
}

void DrawList::RestoreBatchState(const BatchState& s) {
    curTexture_ = s.textureId;
    curFlags_ = s.flags;
    curBlend_ = s.blend;
    clip_ = s.clip;
    batchOpen_ = false;
}

void DrawList::OpenBatch(u32 textureId, u8 flags, BlendMode blend) {
    if (batchOpen_ && !cmds_.empty()) {
        DrawCmd& back = cmds_.back();
        if (back.textureId == textureId && back.flags == flags &&
            back.blend == blend && back.clip == clip_) {
            return; // merge into the open batch
        }
    }

    DrawCmd cmd{};
    cmd.clip = clip_;
    cmd.indexOffset = static_cast<u32>(indices_.size());
    cmd.indexCount = 0;
    cmd.textureId = textureId;
    cmd.blend = blend;
    cmd.flags = flags;
    cmd.pad = 0;
    cmds_.push_back(cmd);
    batchOpen_ = true;
    ++stats_.drawCalls;
}

// ---------------------------------------------------------------------------
// Geometry emission
// ---------------------------------------------------------------------------

Vertex DrawList::MakeCorner(f32 x, f32 y, f32 u, f32 v, const Rect& core,
                            const CornerRadius& r, const Color& fill,
                            const Color& border, const Color& glow,
                            f32 borderWidth, f32 glowWidth, u32 flags) {
    Vertex vert{};
    vert.x = x;
    vert.y = y;
    vert.u = u;
    vert.v = v;
    vert.hx = core.Width() * 0.5f;
    vert.hy = core.Height() * 0.5f;
    vert.rtl = r.tl;
    vert.rtr = r.tr;
    vert.rbr = r.br;
    vert.rbl = r.bl;
    vert.borderWidth = borderWidth;
    vert.glowWidth = glowWidth;
    vert.fill = PackColor(fill);
    vert.border = PackColor(border);
    vert.glowColor = PackColor(glow);
    vert.flags = flags;
    return vert;
}

void DrawList::EmitQuad(const Rect& bounds, f32 angleRadians, const CornerRadius& radiusIn,
                        const Color& fill, const Color& border, const Color& glow,
                        f32 borderWidth, f32 glowWidth, u32 vertexFlags,
                        const Rect& uv, u32 textureId, u8 flags, BlendMode blend) {
    const Rect core = bounds.Shrink(glowWidth);
    if (core.IsEmpty()) return;

    // Radii are clamped against the *core* box so the SDF can never fold over
    // itself, and so the glow band stays uniform around the silhouette.
    const CornerRadius radius = radiusIn.ClampTo(core);
    const f32 ehx = core.Width() * 0.5f + glowWidth;
    const f32 ehy = core.Height() * 0.5f + glowWidth;

    // Corners in the box's own frame, then rotated about the centre. The shader
    // rebuilds the same local coordinates from the interpolated UV, which is
    // why rotation needs no extra vertex data.
    const f32 cx = core.GetCenter().x;
    const f32 cy = core.GetCenter().y;
    const f32 cosA = std::cos(angleRadians);
    const f32 sinA = std::sin(angleRadians);
    auto place = [&](f32 lx, f32 ly, f32& ox, f32& oy) {
        const f32 px = lx * ehx;
        const f32 py = ly * ehy;
        ox = cx + px * cosA - py * sinA;
        oy = cy + px * sinA + py * cosA;
    };

    f32 x0, y0, x1, y1, x2, y2, x3, y3;
    place(-1.0f, -1.0f, x0, y0);
    place( 1.0f, -1.0f, x1, y1);
    place( 1.0f,  1.0f, x2, y2);
    place(-1.0f,  1.0f, x3, y3);

    OpenBatch(textureId, flags, blend);

    const u32 base = static_cast<u32>(vertices_.size());
    vertices_.push_back(MakeCorner(x0, y0, uv.min.x, uv.min.y, core, radius, fill, border, glow, borderWidth, glowWidth, vertexFlags));
    vertices_.push_back(MakeCorner(x1, y1, uv.max.x, uv.min.y, core, radius, fill, border, glow, borderWidth, glowWidth, vertexFlags));
    vertices_.push_back(MakeCorner(x2, y2, uv.max.x, uv.max.y, core, radius, fill, border, glow, borderWidth, glowWidth, vertexFlags));
    vertices_.push_back(MakeCorner(x3, y3, uv.min.x, uv.max.y, core, radius, fill, border, glow, borderWidth, glowWidth, vertexFlags));

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    if (!cmds_.empty()) cmds_.back().indexCount += kIndicesPerQuad;

    ++stats_.quads;
}

void DrawList::EmitTriangle(const Vec2& a, const Vec2& b, const Vec2& c,
                            const Color& color, u32 vertexFlags) {
    OpenBatch(0, static_cast<u8>(CmdFlags::WhiteTexture), curBlend_);

    // Triangles are emitted with a degenerate "core box" sized to the triangle's
    // bounding box so the shared vertex format stays identical. The shader's
    // SDF degenerates to a filled box; the rasteriser clips it to the triangle.
    const Rect bb(Vec2(Min(Min(a.x, b.x), c.x), Min(Min(a.y, b.y), c.y)),
                 Vec2(Max(Max(a.x, b.x), c.x), Max(Max(a.y, b.y), c.y)));

    const u32 base = static_cast<u32>(vertices_.size());
    const Vertex proto = MakeCorner(0, 0, 0, 0, bb, CornerRadius(), color, color, Color(0, 0, 0, 0), 0.0f, 0.0f, vertexFlags);

    Vertex v0 = proto; v0.x = a.x; v0.y = a.y;
    Vertex v1 = proto; v1.x = b.x; v1.y = b.y;
    Vertex v2 = proto; v2.x = c.x; v2.y = c.y;

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);

    if (!cmds_.empty()) cmds_.back().indexCount += 3;
    ++stats_.quads;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

bool DrawList::IsClipped(const Rect& bounds) const {
    return IntersectStrict(clip_, bounds).Area() <= 0.0f;
}

void DrawList::AddBox(const Rect& bounds, const Color& fill, const CornerRadius& radius,
                      f32 borderWidth, const Color& borderColor, f32 glowWidth,
                      const Color& glowColor, u32 vertexFlags) {
    AddBoxRotated(bounds, 0.0f, fill, radius, borderWidth, borderColor, glowWidth, glowColor,
                  vertexFlags);
}

void DrawList::AddBoxRotated(const Rect& bounds, f32 angleRadians, const Color& fill,
                             const CornerRadius& radius, f32 borderWidth, const Color& borderColor,
                             f32 glowWidth, const Color& glowColor, u32 vertexFlags) {
    if (bounds.IsEmpty()) return;

    // Expand the axis-aligned bounds so they contain the rotated quad.
    Rect effective = bounds;
    if (NearlyEqual(angleRadians, 0.0f, 1e-4f) == false) {
        const Vec2 c = bounds.GetCenter();
        const f32 cosA = std::fabs(std::cos(angleRadians));
        const f32 sinA = std::fabs(std::sin(angleRadians));
        const f32 hx = bounds.Width() * 0.5f;
        const f32 hy = bounds.Height() * 0.5f;
        const f32 ex = hx * cosA + hy * sinA;
        const f32 ey = hx * sinA + hy * cosA;
        effective = Rect(c.x - ex, c.y - ey, c.x + ex, c.y + ey);
    }

    if (IsClipped(effective.Expand(glowWidth + 1.0f))) return;

    Color border = borderColor;
    if ((vertexFlags & VF_GRADIENT_MASK) == 0u && border.a <= 0.0f && borderWidth > 0.0f) {
        border = fill; // a border with no colour reads as "same as the fill"
    }

    EmitQuad(bounds, angleRadians, radius, fill, border, glowColor, borderWidth, glowWidth,
             vertexFlags, Rect(0, 0, 1, 1), 0,
             static_cast<u8>(CmdFlags::WhiteTexture), curBlend_);
}

void DrawList::AddLine(const Vec2& a, const Vec2& b, f32 thickness, const Color& color,
                       u32 vertexFlags) {
    const f32 t = Max(thickness, 0.5f);
    const f32 r = t * 0.5f;
    const Vec2 delta = b - a;
    const f32 len = std::sqrt(delta.x * delta.x + delta.y * delta.y);

    if (len <= kEpsilon) {
        AddCircle(Rect(a.x - r, a.y - r, a.x + r, a.y + r), color);
        return;
    }

    // A capsule: a box with fully rounded ends, rotated so its local +Y runs
    // from a to b. Because radii stay circular in the box's own frame this is
    // an exact round-capped stroke at any angle, not an approximation.
    const Vec2 mid = (a + b) * 0.5f;
    const Rect bounds(mid.x - r, mid.y - (len * 0.5f + r), mid.x + r, mid.y + (len * 0.5f + r));
    if (IsClipped(bounds)) return;

    const f32 angle = std::atan2(delta.y, delta.x) - kPi * 0.5f;
    AddBoxRotated(bounds, angle, color, CornerRadius(r), 0.0f, color, 0.0f, Color(0, 0, 0, 0),
                  vertexFlags);
}

void DrawList::AddCircle(const Rect& bounds, const Color& fill, f32 borderWidth,
                         const Color& borderColor) {
    if (bounds.IsEmpty()) return;
    const f32 r = Min(bounds.Width(), bounds.Height()) * 0.5f;
    AddBox(bounds, fill, CornerRadius(r), borderWidth, borderColor);
}

void DrawList::AddArc(const Rect& bounds, f32 startDeg, f32 sweepDeg, f32 thickness,
                      const Color& color) {
    const Vec2 c = bounds.GetCenter();
    const f32 rOuter = Min(bounds.Width(), bounds.Height()) * 0.5f;
    const f32 rInner = Max(rOuter - thickness, 0.0f);
    const f32 steps = Max(2, static_cast<f32>((std::fabs(sweepDeg) / 6.0f) + 1.0f));
    const f32 step = sweepDeg / steps;

    auto point = [&](f32 deg, f32 radius) {
        const f32 rad = deg * (kPi / 180.0f);
        return Vec2(c.x + std::cos(rad) * radius, c.y + std::sin(rad) * radius);
    };

    for (f32 i = 0; i < steps; ++i) {
        const f32 a0 = startDeg + step * i;
        const f32 a1 = a0 + step;
        const Vec2 o0 = point(a0, rOuter), o1 = point(a1, rOuter);
        const Vec2 i0 = point(a0, rInner), i1 = point(a1, rInner);
        EmitTriangle(o0, o1, i1, color, VF_FLAT);
        EmitTriangle(o0, i1, i0, color, VF_FLAT);
    }
}

void DrawList::AddImage(const Rect& bounds, u32 textureId, const Color& tint,
                        const CornerRadius& radius, const Rect& uvRect) {
    if (bounds.IsEmpty() || textureId == 0) return;
    EmitQuad(bounds, 0.0f, radius, tint, tint, Color(0, 0, 0, 0), 0.0f, 0.0f,
             VF_FLAT, uvRect, textureId, static_cast<u8>(CmdFlags::None), curBlend_);
}

void DrawList::AddTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color) {
    EmitTriangle(a, b, c, color, VF_FLAT);
}

void DrawList::AddGlyphQuad(const Rect& bounds, const Rect& uv, const Color& color) {
    if (bounds.IsEmpty()) return;
    EmitQuad(bounds, 0.0f, CornerRadius(), color, color, Color(0, 0, 0, 0),
             0.0f, 0.0f, VF_FLAT | VF_TEX_COVERAGE, uv, curTexture_, curFlags_, curBlend_);
}

// ---------------------------------------------------------------------------

void DrawList::InsertBackgroundBox(const Rect& bounds, const CornerRadius& radiusIn,
                                   const Color& fill, const Color& border, const Color& glow,
                                   f32 borderWidth, f32 glowWidth, u32 checkpoint,
                                   const Rect& clip) {
    if (bounds.IsEmpty()) return;
    if (checkpoint > indices_.size()) return;
    if (IntersectStrict(clip, bounds.Expand(glowWidth)).Area() <= 0.0f) return;

    const Rect core = bounds.Shrink(glowWidth);
    const CornerRadius radius = radiusIn.ClampTo(core);
    const f32 ehx = core.Width() * 0.5f + glowWidth;
    const f32 ehy = core.Height() * 0.5f + glowWidth;
    const f32 cx = core.GetCenter().x;
    const f32 cy = core.GetCenter().y;
    const Rect uv(0.0f, 0.0f, 1.0f, 1.0f);

    // Vertices go on the end of the array; only the *indices* are inserted, so
    // nothing already emitted has to move.
    const u32 base = static_cast<u32>(vertices_.size());
    vertices_.push_back(MakeCorner(cx - ehx, cy - ehy, uv.min.x, uv.min.y, core, radius, fill,
                                   border, glow, borderWidth, glowWidth, VF_FLAT));
    vertices_.push_back(MakeCorner(cx + ehx, cy - ehy, uv.max.x, uv.min.y, core, radius, fill,
                                   border, glow, borderWidth, glowWidth, VF_FLAT));
    vertices_.push_back(MakeCorner(cx + ehx, cy + ehy, uv.max.x, uv.max.y, core, radius, fill,
                                   border, glow, borderWidth, glowWidth, VF_FLAT));
    vertices_.push_back(MakeCorner(cx - ehx, cy + ehy, uv.min.x, uv.max.y, core, radius, fill,
                                   border, glow, borderWidth, glowWidth, VF_FLAT));

    const u32 added = kIndicesPerQuad;

    const u32 tri[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
    indices_.insert(indices_.begin() + static_cast<long>(checkpoint),
                    tri, tri + added);

    // Every existing batch that starts at or after the checkpoint shifts along.
    for (DrawCmd& cmd : cmds_) {
        if (cmd.indexOffset >= checkpoint) cmd.indexOffset += added;
    }

    // If the batch immediately before the checkpoint matches, extend it rather
    // that keeps the insert free in the common case.
    for (size_t i = 0; i < cmds_.size(); ++i) {
        if (cmds_[i].indexOffset >= checkpoint) {
            if (i > 0 && cmds_[i - 1].clip == clip &&
                cmds_[i - 1].textureId == 0 &&
                cmds_[i - 1].flags == static_cast<u8>(CmdFlags::WhiteTexture) &&
                cmds_[i - 1].blend == BlendMode::Alpha) {
                cmds_[i - 1].indexCount += added;
                stats_.quads += 1;
                return;
            }
            DrawCmd cmd{};
            cmd.clip = clip;
            cmd.indexOffset = checkpoint;
            cmd.indexCount = added;
            cmd.textureId = 0;
            cmd.blend = BlendMode::Alpha;
            cmd.flags = static_cast<u8>(CmdFlags::WhiteTexture);
            cmds_.insert(cmds_.begin() + static_cast<long>(i), cmd);
            stats_.quads += 1;
            stats_.drawCalls += 1;
            return;
        }
    }

    // Nothing was emitted after the checkpoint: append a fresh batch.
    DrawCmd cmd{};
    cmd.clip = clip;
    cmd.indexOffset = checkpoint;
    cmd.indexCount = added;
    cmd.textureId = 0;
    cmd.blend = BlendMode::Alpha;
    cmd.flags = static_cast<u8>(CmdFlags::WhiteTexture);
    cmds_.push_back(cmd);
    stats_.quads += 1;
    stats_.drawCalls += 1;
}

// ---------------------------------------------------------------------------

void DrawList::Finalize() {
    stats_.vertices = static_cast<u32>(vertices_.size());
    stats_.indices = static_cast<u32>(indices_.size());
    stats_.drawCalls = static_cast<u32>(cmds_.size());
    stats_.clipRegions = static_cast<u32>(clips_.size());

    // Drop any degenerate trailing batch (can only happen if a caller changed
    // texture/blend after emitting geometry).
    while (!cmds_.empty() && cmds_.back().indexCount == 0) {
        cmds_.pop_back();
    }
    stats_.drawCalls = static_cast<u32>(cmds_.size());
    stats_.batchesMerged = stats_.quads > stats_.drawCalls ? (stats_.quads - stats_.drawCalls) : 0;
}

} // namespace argon::ui