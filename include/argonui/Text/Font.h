// ArgonUI - Text/Font.h
//
// A font is a baked glyph atlas plus a per-codepoint metrics cache. Glyphs are
// rasterised on demand with stb_truetype and shelf-packed into one texture that
// the backend uploads only when it actually changed.
//
// The atlas texture is R8 (raw glyph coverage). The shader is told via
// VF_TEX_COVERAGE that the bound texture holds coverage in .r, which is a
// quarter of the VRAM an RGBA8 atlas would cost and the fragment path is
// identical to the one used for images and flat fills.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../Backend/Backend.h"
#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "../Draw/DrawList.h"

namespace argon::ui {
/// UTF-8 decoding. Returns the codepoint and advances `i`.
u32 DecodeUtf8(const char* text, size_t length, size_t& i);
u32 DecodeUtf8(const std::string& text, size_t& i);
/// Appends a codepoint as UTF-8. Returns bytes written.
size_t EncodeUtf8(u32 codepoint, char* out);

/// Per-glyph baked data.
struct Glyph {
    f32 width = 0.0f;       ///< Bitmap width, px.
    f32 height = 0.0f;      ///< Bitmap height, px.
    /// Pen-relative bitmap left offset, in pixels (screen x).
    f32 bearingX = 0.0f;
    /// Pen-relative bitmap top offset, in pixels, in screen space where
    /// Y grows downward. Negative for anything above the baseline, positive
    /// for descenders. Draw the bitmap at (pen.x + bearingX, pen.y + bearingY).
    f32 bearingY = 0.0f;
    f32 advanceX = 0.0f;    ///< Pen advance in pixels.
    f32 advanceY = 0.0f;
    Rect uv;                ///< Atlas UV in [0,1]. Empty for whitespace.
    bool baked = false;
    bool isWhitespace = false;
};

/// Options for Font::Build / FontManager.
struct FontDesc {
    f32 pixelSize = 15.0f;      ///< Em size in pixels.
    f32 dpiScale = 1.0f;        ///< Applied on top of pixelSize.
    f32 lineGap = 1.0f;         ///< Extra pixels added between lines.
    /// Rasterisation oversample. 1 is native; 1.5-2 gives noticeably cleaner
    /// diagonals at the cost of atlas memory.
    f32 oversample = 1.25f;
    f32 hintLevel = 1.0f;       ///< 0 = unhinted (smoother, blurry small)
                                ///< 1 = normal
    u32 atlasWidth = 1024;
    u32 atlasHeight = 1024;
    bool italic = false;
    bool bold = false;
};

class Font {
public:
    Font();
    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    /// Takes ownership of a copy of `data`.
    bool LoadFromMemory(const void* data, size_t size, const FontDesc& desc);
    bool LoadFromFile(const char* path, const FontDesc& desc);

    /// Walks a small, well-known list of system fonts. Works on Windows and on
    /// most Linux/macOS installs. Returns false if nothing usable was found.
    static bool LoadSystemDefault(Font& out, const FontDesc& desc,
                                  const char* familyHint = nullptr);

    void Clear();
    bool Valid() const { return valid_; }

    f32 PixelSize() const { return pixelSize_; }
    f32 LineHeight() const { return lineHeight_; }
    f32 Ascent() const { return ascent_; }
    f32 Descent() const { return descent_; }

    // -- Atlas -------------------------------------------------------------

    /// True when new glyphs have been baked since the last upload.
    bool AtlasDirty() const { return atlasDirty_; }
    void MarkAtlasClean() { atlasDirty_ = false; }

    const u8* AtlasPixels() const { return atlas_.data(); }
    u32 AtlasWidth() const { return atlasW_; }
    u32 AtlasHeight() const { return atlasH_; }
    TextureFormat AtlasFormat() const { return TextureFormat::R8; }

    /// Releases cached glyphs and rebuilds the atlas from scratch.
    void ClearGlyphCache();

    // -- Metrics -----------------------------------------------------------

    /// Returns the glyph, baking it if necessary. Never returns nullptr for a
    /// loaded font (unknown codepoints resolve to the fallback glyph).
    const Glyph* GetGlyph(u32 codepoint);

    /// Width of `text` at this font, honouring \\n.
    Vec2 MeasureText(const char* text, size_t length) const;
    Vec2 MeasureText(const std::string& text) const;

    /// Width of a single line, no wrapping.
    f32 MeasureLine(const char* text, size_t length) const;
    f32 AdvanceOf(u32 codepoint) const;

    /// Truncates to fit `maxWidth`, appending `ellipsis` when it does not fit.
    /// Returns bytes written (excluding the terminator).
    size_t Ellipsize(const char* text, size_t length, f32 maxWidth,
                     const char* ellipsis = "...") const;

    /// Greedy word wrap. Returns the number of lines written to `lines`
    /// (each a {offset, length} pair into `text`). Lines never exceed maxWidth
    /// unless a single word is longer than maxWidth.
    size_t WrapText(const char* text, size_t length, f32 maxWidth,
                    std::vector<std::pair<size_t, size_t>>& lines) const;

    f32 Kerning(u32 first, u32 second) const;

private:
    struct Impl;

    bool BuildAtlasIfNeeded();
    bool AllocateGlyph(u32 codepoint, Glyph& out);
    const Glyph* GetFallbackGlyph();
    void ResetAtlas();

    std::unique_ptr<Impl> impl_;

    bool valid_ = false;
    FontDesc desc_{};

    f32 pixelSize_ = 0.0f;
    f32 lineHeight_ = 0.0f;
    f32 ascent_ = 0.0f;
    f32 descent_ = 0.0f;

    /// Single-channel glyph coverage atlas.
    std::vector<u8> atlas_;
    u32 atlasW_ = 1024;
    u32 atlasH_ = 1024;
    u32 penX_ = 0;
    u32 penY_ = 0;
    u32 shelfHeight_ = 0;
    bool atlasDirty_ = false;

    std::unordered_map<u32, Glyph> glyphs_;
    u32 fallbackCodepoint_ = '?';
    mutable std::vector<u8> scratch_;
};

/// Owns every font the UI needs: each distinct pixel size gets its own baked
/// atlas (R8, typically 512x512 = 256 KB), created lazily the first time a
/// widget asks for that size. Keeping atlases per size, rather than scaling one
/// atlas, is why ArgonUI stays crisp at 125% / 150% / 200% DPI and across
/// the type scale in the reference UI (11 / 12 / 14 / 18 / 24 px).
///
/// Entries are heap-allocated and never move, so a Font* handed out once stays
/// valid for the lifetime of the manager.
class FontManager {
public:
    FontManager();
    ~FontManager();

    /// Builds the default family. `familyDir` may point at a directory of TTFs;
    /// when null, a system font is located automatically.
    bool Initialize(const char* familyDir = nullptr, f32 dpiScale = 1.0f);

    /// Mark the family dirty; atlases are re-baked at the next AtSize() call.
    /// Cheap to call every frame - it only takes effect when the scale changes.
    void SetDpiScale(f32 dpiScale);
    f32 DpiScale() const { return dpiScale_; }

    /// Font for a logical pixel size. Creates the atlas on first use.
    Font* AtSize(f32 logicalSize);
    Font* SemiboldAtSize(f32 logicalSize);
    Font* MonoAtSize(f32 logicalSize);

    /// Body-size regular/semibold/mono. Convenience for the common case.
    Font* Regular() { return AtSize(FontManager::kSizeBody); }
    Font* Semibold() { return SemiboldAtSize(FontManager::kSizeBody); }
    Font* Mono() { return MonoAtSize(FontManager::kSizeBody); }

    /// Uploads/re-uploads any dirty atlas. Returns true if anything changed.
    bool SyncTextures(class IArgonBackend& backend);

    /// Releases GPU textures but keeps the CPU-side atlases.
    void ReleaseTextures(class IArgonBackend& backend);

    /// Drops every atlas, forcing a full re-bake. Used on a big DPI change.
    void Invalidate();

    u32 TextureIdOf(const Font* font) const;

    /// Default weights used across the reference UI.
    static constexpr f32 kSizeBody = 14.0f;
    static constexpr f32 kSizeSmall = 12.0f;
    static constexpr f32 kSizeTiny = 11.0f;
    static constexpr f32 kSizeTitle = 18.0f;
    static constexpr f32 kSizeDisplay = 24.0f;

private:
    struct Entry {
        f32 logicalSize = 0.0f;
        bool semibold = false;
        bool mono = false;
        std::unique_ptr<Font> font;
        u32 textureId = 0;
    };

    Font* Find(f32 logicalSize, int face);
    Font* Create(f32 logicalSize, int face);
    void ApplyDpiIfDirty();

    enum Face { FaceRegular = 0, FaceSemibold = 1, FaceMono = 2 };

    std::vector<std::unique_ptr<Entry>> entries_;
    std::vector<u8> regularTtf_;
    std::vector<u8> semiboldTtf_;
    std::vector<u8> monoTtf_;

    bool initialized_ = false;
    bool dpiDirty_ = false;
    f32 dpiScale_ = 1.0f;
    f32 appliedDpi_ = 1.0f;
    std::string familyDir_;
};

} // namespace argon::ui
