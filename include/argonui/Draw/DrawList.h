// ArgonUI - Draw/DrawList.h
//
// A DrawList is the complete, backend-agnostic output of one ArgonUI frame.
//
// The UI core emits quads into a shared vertex/index pair and greedily merges
// consecutive quads into batches whenever (clip rect, texture, blend, flags)
// all match. In practice a typical menu frame collapses to 3-8 draw calls
// even with a few thousand primitives, because widgets flush their text runs
// and shapes in large contiguous runs.
//
// The backend's entire job is: upload vertices + indices, set a scissor rect
// and a texture per batch, and issue DrawIndexed. It never sees a widget.
#pragma once

#include <vector>

#include "../Core/Color.h"
#include "../Core/Log.h"
#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "Vertex.h"

namespace argon::ui {
enum class BlendMode : u8 {
    /// Premultiplied source alpha: (ONE, ONE_MINUS_SRC_ALPHA).
    /// Everything ArgonUI draws uses this by default.
    Alpha = 0,
    /// Additive: (ONE, ONE). Accent glows, energy bars, laser lines.
    Additive = 1,
};

/// Batch-level flags (not the per-vertex VF_* flags).
enum class CmdFlags : u8 {
    None = 0,
    /// Texture is the built-in white 1x1 (shapes, not glyphs/images).
    WhiteTexture = 1u << 0,
};

/// One merged batch of quads.
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
static_assert(sizeof(DrawCmd) == 32, "ArgonUI batch layout");

/// Per-frame statistics, surfaced by the debug overlay.
struct DrawStats {
    u32 vertices = 0;
    u32 indices = 0;
    u32 quads = 0;
    u32 drawCalls = 0;   ///< == CmdCount() after the backend submits.
    u32 clipRegions = 0;
    u32 batchesMerged = 0;   ///< quads that joined an existing batch instead of starting one.
};

/// Accumulates primitives and merges them into batches.
class DrawList {
public:
    DrawList();

    // -- Lifetime ----------------------------------------------------------
    void Reset();
    void Reserve(size_t vertexCapacity, size_t indexCapacity, size_t cmdCapacity);

    // -- Clipping ----------------------------------------------------------
    /// Intersects the current clip with `rect` and returns the previous one.
    /// The returned value must be handed back to PopClip().
    Rect PushClip(const Rect& rect);
    void PopClip(const Rect& previousClip);
    const Rect& CurrentClip() const { return clip_; }

    // -- Texture / batch state --------------------------------------------
    void SetTexture(u32 textureId, CmdFlags flags);
    void SetBlend(BlendMode blend);
    /// Round-trip guard so a widget can restore the batch state it inherited.
    struct BatchState { u32 textureId; u8 flags; BlendMode blend; Rect clip; };
    BatchState SaveBatchState() const;
    void RestoreBatchState(const BatchState& s);

    // -- Primitives --------------------------------------------------------

    /// The general-purpose primitive: rounded box with fill, inner border,
    /// optional gradient and optional outer glow. Everything else funnels here.
    void AddBox(const Rect& bounds,
                const Color& fill,
                const CornerRadius& radius = CornerRadius(),
                f32 borderWidth = 0.0f,
                const Color& borderColor = Color(0, 0, 0, 0),
                f32 glowWidth = 0.0f,
                const Color& glowColor = Color(0, 0, 0, 0),
                u32 vertexFlags = VF_FLAT);

    /// Same, rotated by `angleRadians` about the box's centre.
    ///
    /// Rotation costs nothing extra: the fragment shader reconstructs its
    /// local coordinates from the interpolated UV, so a rotated quad is a quad
    /// whose four corners were rotated on the CPU. Radii stay circular in the
    /// box's own frame, which suits strokes and capsules.
    void AddBoxRotated(const Rect& bounds, f32 angleRadians,
                       const Color& fill,
                       const CornerRadius& radius = CornerRadius(),
                       f32 borderWidth = 0.0f,
                       const Color& borderColor = Color(0, 0, 0, 0),
                       f32 glowWidth = 0.0f,
                       const Color& glowColor = Color(0, 0, 0, 0),
                       u32 vertexFlags = VF_FLAT);

    /// Straight capsule with round caps, at any angle.
    void AddLine(const Vec2& a, const Vec2& b,
                 f32 thickness, const Color& color, u32 vertexFlags = VF_FLAT);

    /// Circle inscribed in `bounds`.
    void AddCircle(const Rect& bounds, const Color& fill,
                   f32 borderWidth = 0.0f,
                   const Color& borderColor = Color(0, 0, 0, 0));

    /// Ring / arc segment. `startDeg`/`sweepDeg` measured clockwise from 3 o'clock.
    /// Used by radial gauges and the arc accents in the reference UI.
    void AddArc(const Rect& bounds, f32 startDeg, f32 sweepDeg, f32 thickness,
                const Color& color);

    /// Textured quad, clipped to a rounded box. `tint` multiplies the texture.
    /// `uvRect` selects a sub-rectangle of the texture (for sprite sheets).
    void AddImage(const Rect& bounds,
                  u32 textureId,
                  const Color& tint = Color(1, 1, 1, 1),
                  const CornerRadius& radius = CornerRadius(),
                  const Rect& uvRect = Rect(0, 0, 1, 1));

    /// Triangles, for the rare cases that are genuinely not a box (arrows,
    /// chevrons drawn as polygons, progress spikes).
    void AddTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color);

    /// Scissor-only helper: forces the current clip without emitting geometry.
    /// Lets a backend skip a batch whose geometry is entirely clipped away.
    bool IsClipped(const Rect& bounds) const;
    bool IsFullyClipped() const { return clip_.IsEmpty(); }

    // -- Glyph submission --------------------------------------------------
    /// Emits one glyph quad. The font atlas texture must already be current.
    void AddGlyphQuad(const Rect& bounds,
                      const Rect& uv,
                      const Color& color);

    /// Records the current index count. Pass it to InsertBackgroundBox later.
    u32 IndexCheckpoint() const { return static_cast<u32>(indices_.size()); }

    /// Inserts a primitive immediately *before* `checkpoint`, after everything
    /// already emitted.
    ///
    /// Lets a container paint its background behind content whose height is only
    /// known at End time: collapsible sections, cards sized to their rows,
    /// expanding groups. ImGui handles this by making the caller declare a child
    /// size up front. Rewriting the index buffer costs O(draw calls), not
    /// O(vertices).
    ///
    /// `clip` must be the clip that was active at `checkpoint`, not the one
    /// active now.
    void InsertBackgroundBox(const Rect& bounds, const CornerRadius& radius, const Color& fill,
                             f32 borderWidth, const Color& border, f32 glowWidth,
                             const Color& glowColor, u32 checkpoint, const Rect& clip);

    // -- Accessors ---------------------------------------------------------
    const Vertex* Vertices() const { return vertices_.data(); }
    u32 VertexCount() const { return static_cast<u32>(vertices_.size()); }

    const u32* Indices() const { return indices_.data(); }
    u32 IndexCount() const { return static_cast<u32>(indices_.size()); }

    const DrawCmd* Cmds() const { return cmds_.data(); }
    u32 CmdCount() const { return static_cast<u32>(cmds_.size()); }

    const Rect& ClipRegion(u32 index) const { return clips_[index]; }
    u32 ClipRegionCount() const { return static_cast<u32>(clips_.size()); }
    const Rect* Clips() const { return clips_.data(); }

    const DrawStats& Stats() const { return stats_; }

    /// Total bytes the backend would upload this frame.
    size_t UploadBytes() const {
        return vertices_.size() * sizeof(Vertex) + indices_.size() * sizeof(u32);
    }

    /// Fills in Stats and drops a trailing batch that carries no geometry.
    /// Called once by Context before submission. Merging happens during
    /// emission in OpenBatch, not here.
    void Finalize();

private:
    /// Opens a batch, or silently reuses the currently open one when the
    /// (texture, flags, blend, clip) tuple matches exactly. All geometry
    /// emitted while a batch is open is merged into it.
    void OpenBatch(u32 textureId, u8 flags, BlendMode blend);

    void EmitQuad(const Rect& bounds, f32 angleRadians, const CornerRadius& radius,
                  const Color& fill, const Color& border, const Color& glow,
                  f32 borderWidth, f32 glowWidth, u32 vertexFlags,
                  const Rect& uv, u32 textureId, u8 flags, BlendMode blend);

    void EmitTriangle(const Vec2& a, const Vec2& b, const Vec2& c,
                      const Color& color, u32 vertexFlags);

    /// Interned clip rect. Deduplicated so we never hand the backend a
    /// scissor list with the same rect listed twice.
    u32 InternClip(const Rect& r);

    static Vertex MakeCorner(f32 x, f32 y, f32 u, f32 v, const Rect& core,
                             const CornerRadius& r, const Color& fill,
                             const Color& border, const Color& glow,
                             f32 borderWidth, f32 glowWidth, u32 flags);

    std::vector<Vertex> vertices_;
    std::vector<u32> indices_;
    std::vector<DrawCmd> cmds_;
    std::vector<Rect> clips_;

    Rect clip_;
    Rect lastClip_;        ///< clip at the time the last batch was opened
    u32 curTexture_ = 0;
    u8 curFlags_ = static_cast<u8>(CmdFlags::WhiteTexture);
    BlendMode curBlend_ = BlendMode::Alpha;

    /// True when the currently-open batch is still appendable.
    bool batchOpen_ = false;

    DrawStats stats_;
};

} // namespace argon::ui
