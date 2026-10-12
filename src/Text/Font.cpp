// ArgonUI - Text/Font.cpp
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "argonui/Core/Log.h"
#include "argonui/Text/Font.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace argon::ui {

namespace {

// ---------------------------------------------------------------------------
// File IO (portable, no platform SDK pulled in)
// ---------------------------------------------------------------------------
bool ReadFile(const char* path, std::vector<u8>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long size = std::ftell(f);
    if (size <= 0) { std::fclose(f); return false; }
    std::rewind(f);
    out.resize(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) { out.clear(); return false; }
    return true;
}

/// Candidate TTF paths, most-preferred first, per role.
struct FamilyCandidates {
    const char* regular[8];
    const char* semibold[8];
    const char* mono[8];
};

const FamilyCandidates kFamilies[] = {
    {   // Inter - the typeface the reference UI uses.
        { "Fonts/Inter-Regular.ttf", "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
          "/usr/local/share/fonts/Inter-Regular.ttf", nullptr },
        { "Fonts/Inter-SemiBold.ttf", "/usr/share/fonts/truetype/inter/Inter-SemiBold.ttf",
          "/usr/local/share/fonts/Inter-SemiBold.ttf", nullptr },
        { "Fonts/Inter-Regular.ttf", "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
          nullptr, nullptr },
    },
    {   // Windows system UI font.
        { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr },
        { "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf",
          "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", nullptr },
        { "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/CascadiaMono.ttf",
          "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", nullptr },
    },
    {   // Roboto - Android / Chrome OS.
        { "/usr/share/fonts/truetype/roboto/Roboto-Regular.ttf",
          "/usr/share/fonts/TTF/Roboto-Regular.ttf", "C:/Windows/Fonts/arial.ttf", nullptr },
        { "/usr/share/fonts/truetype/roboto/Roboto-Medium.ttf",
          "/usr/share/fonts/TTF/Roboto-Medium.ttf", "C:/Windows/Fonts/arialbd.ttf", nullptr },
        { "/usr/share/fonts/truetype/roboto/RobotoMono-Regular.ttf",
          "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", nullptr, nullptr },
    },
};

std::string ResolvePath(const char* relOrAbs, const char* fontDir) {
    std::string s(relOrAbs ? relOrAbs : "");
    if (s.empty()) return s;
    // "Fonts/x.ttf" is relative to the Windows font directory.
    if (s.compare(0, 6, "Fonts/") == 0) {
        std::string full = "C:/Windows/";
        full += s;
        return full;
    }
    if (fontDir != nullptr && fontDir[0] != '\0' && s[0] != '/' && s.find(':') == std::string::npos) {
        std::string full = fontDir;
        if (!full.empty() && full.back() != '/' && full.back() != '\\') full += '/';
        full += s;
        return full;
    }
    return s;
}

bool FindAndLoad(const char* const* candidates, const char* fontDir,
                 std::vector<u8>& outData) {
    for (int i = 0; i < 8; ++i) {
        if (candidates[i] == nullptr) break;
        const std::string path = ResolvePath(candidates[i], fontDir);
        if (ReadFile(path.c_str(), outData)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------
constexpr u32 kReplacementChar = 0xFFFDu;

} // namespace

u32 DecodeUtf8(const char* text, size_t length, size_t& i) {
    if (text == nullptr || i >= length) return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    const unsigned char c0 = p[i];
    if (c0 < 0x80u) { ++i; return c0; }

    auto cont = [&](size_t k) -> bool {
        return (i + k) < length && (p[i + k] & 0xC0u) == 0x80u;
    };

    if ((c0 & 0xE0u) == 0xC0u && cont(1)) {
        const u32 cp = ((c0 & 0x1Fu) << 6) | (p[i + 1] & 0x3Fu);
        i += 2;
        return cp < 0x80u ? kReplacementChar : cp;
    }
    if ((c0 & 0xF0u) == 0xE0u && cont(1) && cont(2)) {
        const u32 cp = ((c0 & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu);
        i += 3;
        return cp < 0x800u ? kReplacementChar : cp;
    }
    if ((c0 & 0xF8u) == 0xF0u && cont(1) && cont(2) && cont(3)) {
        const u32 cp = ((c0 & 0x07u) << 18) | ((p[i + 1] & 0x3Fu) << 12) |
                       ((p[i + 2] & 0x3Fu) << 6) | (p[i + 3] & 0x3Fu);
        i += 4;
        return (cp < 0x10000u || cp > 0x10FFFFu) ? kReplacementChar : cp;
    }

    ++i; // resynchronise on the bad byte
    return kReplacementChar;
}

u32 DecodeUtf8(const std::string& text, size_t& i) {
    return DecodeUtf8(text.data(), text.size(), i);
}

size_t EncodeUtf8(u32 cp, char* out) {
    if (out == nullptr) return 0;
    if (cp < 0x80u) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (cp >> 6));
        out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = static_cast<char>(0xE0u | (cp >> 12));
        out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = static_cast<char>(0xF0u | (cp >> 18));
    out[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
    return 4;
}

// ---------------------------------------------------------------------------
// Font::Impl
// ---------------------------------------------------------------------------

struct Font::Impl {
    std::vector<u8> ttf;
    stbtt_fontinfo info{};
    f32 scale = 0.0f;        ///< Font units -> pixels.
    f32 oversampleScale = 0.0f;
    bool infoValid = false;
};

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

Font::Font() : impl_(std::make_unique<Impl>()) { ResetAtlas(); }

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

void Font::ResetAtlas() {
    atlasW_ = Max<u32>(64u, desc_.atlasWidth);
    atlasH_ = Max<u32>(64u, desc_.atlasHeight);
    atlas_.assign(static_cast<size_t>(atlasW_) * atlasH_, 0u);
    penX_ = 0;
    penY_ = 0;
    shelfHeight_ = 0;
    atlasDirty_ = false;
}

void Font::Clear() {
    impl_ = std::make_unique<Impl>();
    glyphs_.clear();
    valid_ = false;
    desc_ = FontDesc{};
    pixelSize_ = 0.0f;
    lineHeight_ = 0.0f;
    ascent_ = 0.0f;
    descent_ = 0.0f;
    ResetAtlas();
}

void Font::ClearGlyphCache() {
    glyphs_.clear();
    atlas_.assign(atlas_.size(), 0u);
    penX_ = 0;
    penY_ = 0;
    shelfHeight_ = 0;
    atlasDirty_ = true;
}

bool Font::LoadFromMemory(const void* data, size_t size, const FontDesc& desc) {
    if (data == nullptr || size == 0) return false;

    impl_->ttf.assign(static_cast<const u8*>(data),
                      static_cast<const u8*>(data) + size);

    const int offset = stbtt_GetFontOffsetForIndex(impl_->ttf.data(), 0);
    impl_->infoValid = stbtt_InitFont(&impl_->info, impl_->ttf.data(), offset) != 0;
    if (!impl_->infoValid) {
        impl_->ttf.clear();
        return false;
    }

    desc_ = desc;
    atlasW_ = Max<u32>(64u, desc.atlasWidth);
    atlasH_ = Max<u32>(64u, desc.atlasHeight);

    pixelSize_ = Max(1.0f, desc.pixelSize * Max(0.1f, desc.dpiScale));
    impl_->scale = stbtt_ScaleForPixelHeight(&impl_->info, pixelSize_);
    impl_->oversampleScale = impl_->scale * Max(1.0f, desc.oversample);

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
    ascent_ = static_cast<f32>(ascent) * impl_->scale;
    descent_ = static_cast<f32>(descent) * impl_->scale;
    lineHeight_ = (static_cast<f32>(ascent + descent + lineGap) * impl_->scale) + desc.lineGap;

    if (ascent_ <= 0.0f) ascent_ = pixelSize_ * 0.8f;
    if (descent_ <= 0.0f) descent_ = pixelSize_ * 0.2f;
    if (lineHeight_ <= 0.0f) lineHeight_ = pixelSize_ * 1.25f + desc.lineGap;

    atlas_.assign(static_cast<size_t>(atlasW_) * atlasH_, 0u);
    penX_ = 0;
    penY_ = 0;
    shelfHeight_ = 0;
    glyphs_.clear();
    atlasDirty_ = true;
    valid_ = true;

    // Pre-bake ASCII so the first frame has no glyph-upload hitch.
    for (u32 cp = 32; cp < 127; ++cp) GetGlyph(cp);

    return true;
}

bool Font::LoadFromFile(const char* path, const FontDesc& desc) {
    std::vector<u8> data;
    if (!ReadFile(path, data)) return false;
    return LoadFromMemory(data.data(), data.size(), desc);
}

bool Font::LoadSystemDefault(Font& out, const FontDesc& desc, const char* familyHint) {
    std::vector<u8> data;

    // An explicit path always wins.
    if (familyHint != nullptr && familyHint[0] != '\0') {
        if (ReadFile(familyHint, data)) {
            return out.LoadFromMemory(data.data(), data.size(), desc);
        }
    }

    const size_t familyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);
    for (size_t f = 0; f < familyCount; ++f) {
        if (FindAndLoad(kFamilies[f].regular, nullptr, data)) {
            return out.LoadFromMemory(data.data(), data.size(), desc);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Glyph baking
// ---------------------------------------------------------------------------

const Glyph* Font::GetFallbackGlyph() {
    auto it = glyphs_.find(fallbackCodepoint_);
    if (it != glyphs_.end() && it->second.baked) return &it->second;
    return nullptr;
}

bool Font::AllocateGlyph(u32 codepoint, Glyph& out) {
    if (!valid_ || !impl_->infoValid) return false;

    const int glyphIndex = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint));
    if (glyphIndex == 0) {
        // Not in this font. Fall back once; a second miss is a hard failure.
        if (codepoint == fallbackCodepoint_) return false;
        Glyph fb{};
        if (!AllocateGlyph(fallbackCodepoint_, fb)) return false;
        // Keep the fallback's advance so layout does not jump, but let the
        // caller draw the fallback glyph.
        out = fb;
        out.advanceX = 0.0f;
        out.baked = true;
        out.isWhitespace = false;
        return true;
    }

    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, glyphIndex, &advance, &lsb);
    (void)lsb;

    out = Glyph{};
    out.advanceX = static_cast<f32>(advance) * impl_->scale;
    out.advanceY = 0.0f;

    if (out.advanceX == 0.0f) {
        out.isWhitespace = true;
        out.baked = true;
        return true;
    }

    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    stbtt_GetCodepointBitmapBox(&impl_->info, glyphIndex, impl_->oversampleScale,
                                impl_->oversampleScale, &ix0, &iy0, &ix1, &iy1);
    if (ix1 <= ix0 || iy1 <= iy0) {
        // Renders to nothing (e.g. a zero-width joiner). Treat as blank.
        out.isWhitespace = true;
        out.baked = true;
        return true;
    }

    const f32 invOver = 1.0f / Max(1.0f, desc_.oversample);
    const int dw = Max(1, static_cast<int>(std::ceil(static_cast<f32>(ix1 - ix0) * invOver)));
    const int dh = Max(1, static_cast<int>(std::ceil(static_cast<f32>(iy1 - iy0) * invOver)));

    out.width = static_cast<f32>(dw);
    out.height = static_cast<f32>(dh);
    out.bearingX = static_cast<f32>(ix0) * invOver;
    out.bearingY = static_cast<f32>(iy0) * invOver;

    // Shelf packing. 1px of padding stops bilinear taps bleeding across glyphs.
    constexpr u32 kPad = 1;
    if (penX_ + static_cast<u32>(dw) + kPad > atlasW_) {
        penX_ = 0;
        penY_ += shelfHeight_ + kPad;
        shelfHeight_ = 0;
    }
    if (penY_ + static_cast<u32>(dh) + kPad > atlasH_) {
        // Grow downward. Existing glyphs keep their coordinates and UVs, so
        // nothing already emitted this frame becomes invalid.
        constexpr u32 kMaxAtlasHeight = 8192;
        if (atlasH_ < kMaxAtlasHeight) {
            const u32 newHeight = Min(kMaxAtlasHeight, atlasH_ * 2u);
            const size_t stride = static_cast<size_t>(atlasW_);
            atlas_.resize(stride * newHeight, 0u);
            atlasH_ = newHeight;
        } else {
            return false; // hard full
        }
    }

    // Rasterise into a tight 8bpp scratch buffer, then area-average down.
    if (scratch_.size() < static_cast<size_t>(ix1 - ix0) * static_cast<size_t>(iy1 - iy0)) {
        scratch_.resize(static_cast<size_t>(ix1 - ix0) * static_cast<size_t>(iy1 - iy0));
    }
    const int sw = ix1 - ix0;
    const int sh = iy1 - iy0;
    stbtt_MakeCodepointBitmap(&impl_->info, scratch_.data(), sw, sh, sw,
                              impl_->oversampleScale, impl_->oversampleScale, glyphIndex);

    const f32 ratio = static_cast<f32>(sw) / static_cast<f32>(dw);
    const size_t atlasStride = static_cast<size_t>(atlasW_);
    for (int dy = 0; dy < dh; ++dy) {
        const int y0s = static_cast<int>(static_cast<f32>(dy) * ratio);
        const int y1s = static_cast<int>(static_cast<f32>(dy + 1) * ratio);
        const int yStart = Clamp(Min(y0s, sh - 1), 0, sh);
        const int yEnd = Clamp(Max(y0s + 1, y1s), 1, sh);
        u8* dstRow = atlas_.data() + (penY_ + static_cast<u32>(dy)) * atlasStride + penX_;
        for (int dx = 0; dx < dw; ++dx) {
            const int x0s = static_cast<int>(static_cast<f32>(dx) * ratio);
            const int x1s = static_cast<int>(static_cast<f32>(dx + 1) * ratio);
            const int xStart = Clamp(x0s, 0, sw - 1);
            const int xEnd = Clamp(Max(x1s, x0s + 1), 1, sw);

            u32 accum = 0;
            u32 count = 0;
            for (int sy = yStart; sy < Min(yEnd, sh); ++sy) {
                const u8* srcRow = scratch_.data() + static_cast<size_t>(sy) * sw;
                for (int sx = xStart; sx < Min(xEnd, sw); ++sx) {
                    accum += srcRow[sx];
                    ++count;
                }
            }
            dstRow[dx] = static_cast<u8>(count > 0 ? (accum / count) : 0u);
        }
    }

    out.uv = Rect(static_cast<f32>(penX_) / static_cast<f32>(atlasW_),
                  static_cast<f32>(penY_) / static_cast<f32>(atlasH_),
                  static_cast<f32>(penX_ + static_cast<u32>(dw)) / static_cast<f32>(atlasW_),
                  static_cast<f32>(penY_ + static_cast<u32>(dh)) / static_cast<f32>(atlasH_));

    penX_ += static_cast<u32>(dw) + kPad;
    shelfHeight_ = Max(shelfHeight_, static_cast<u32>(dh));
    atlasDirty_ = true;
    out.baked = true;
    out.isWhitespace = false;
    return true;
}

const Glyph* Font::GetGlyph(u32 codepoint) {
    if (!valid_) return nullptr;

    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end() && it->second.baked) return &it->second;

    Glyph baked{};
    AllocateGlyph(codepoint, baked);
    auto inserted = glyphs_.emplace(codepoint, baked);
    return &inserted.first->second;
}

f32 Font::AdvanceOf(u32 codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end() && it->second.baked) return it->second.advanceX;
    if (!valid_) return 0.0f;
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, stbtt_FindGlyphIndex(&impl_->info,
                                                                   static_cast<int>(codepoint)),
                               &advance, &lsb);
    return static_cast<f32>(advance) * impl_->scale;
}

f32 Font::Kerning(u32 first, u32 second) const {
    if (!valid_ || desc_.pixelSize < 1.0f) return 0.0f;
    const int g0 = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(first));
    const int g1 = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(second));
    if (g0 == 0 || g1 == 0) return 0.0f;
    return static_cast<f32>(stbtt_GetGlyphKernAdvance(&impl_->info, g0, g1)) * impl_->scale;
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

f32 Font::MeasureLine(const char* text, size_t length) const {
    if (!valid_ || text == nullptr || length == 0) return 0.0f;
    f32 width = 0.0f;
    u32 prev = 0;
    size_t i = 0;
    while (i < length) {
        const u32 cp = DecodeUtf8(text, length, i);
        width += AdvanceOf(cp);
        if (prev != 0) width += Kerning(prev, cp);
        prev = cp;
    }
    return width;
}

Vec2 Font::MeasureText(const char* text, size_t length) const {
    if (!valid_ || text == nullptr) return Vec2(0.0f, lineHeight_);
    f32 widest = 0.0f;
    f32 current = 0.0f;
    u32 lines = 1;
    u32 prev = 0;
    size_t i = 0;
    while (i < length) {
        if (text[i] == '\n') {
            widest = Max(widest, current);
            current = 0.0f;
            ++lines;
            prev = 0;
            ++i;
            continue;
        }
        const u32 cp = DecodeUtf8(text, length, i);
        if (prev != 0) current += Kerning(prev, cp);
        current += AdvanceOf(cp);
        prev = cp;
    }
    widest = Max(widest, current);
    return Vec2(widest, lineHeight_ * static_cast<f32>(lines));
}

Vec2 Font::MeasureText(const std::string& text) const {
    return MeasureText(text.data(), text.size());
}

size_t Font::Ellipsize(const char* text, size_t length, f32 maxWidth,
                       const char* ellipsis) const {
    if (!valid_ || text == nullptr) return 0;
    const size_t ellLen = ellipsis != nullptr ? std::strlen(ellipsis) : 0;
    const f32 ellWidth = ellLen > 0 ? MeasureLine(ellipsis, ellLen) : 0.0f;

    // Largest prefix P such that width(P) + ellWidth <= maxWidth. Measured
    // forward with a monotonically increasing width so the scan is O(n) and
    // never has to re-measure a suffix.
    f32 width = 0.0f;
    size_t fit = 0;
    size_t i = 0;
    u32 prev = 0;
    while (i < length) {
        const size_t start = i;
        const u32 cp = DecodeUtf8(text, length, i);
        f32 advance = AdvanceOf(cp);
        if (prev != 0) advance += Kerning(prev, cp);
        if (width + advance + ellWidth > maxWidth) break;
        width += advance;
        prev = cp;
        fit = i;
        (void)start;
    }

    // The whole string fits.
    if (fit == length) return length;
    // Nothing fits alongside the ellipsis: emit the ellipsis alone.
    if (fit == 0) return Min(length, ellLen);
    return fit;
}

size_t Font::WrapText(const char* text, size_t length, f32 maxWidth,
                      std::vector<std::pair<size_t, size_t>>& lines) const {
    lines.clear();
    if (!valid_ || text == nullptr || length == 0) return 0;

    size_t lineStart = 0;   ///< Byte offset where the current line begins.
    size_t cursor = 0;      ///< Next byte to decode.
    size_t breakPos = std::string::npos;  ///< Byte offset just past the last space.
    f32 lineWidth = 0.0f;   ///< Width of [lineStart, cursor).
    u32 prev = 0;

    while (cursor < length) {
        if (text[cursor] == '\n') {
            lines.emplace_back(lineStart, cursor - lineStart);
            ++cursor;
            lineStart = cursor;
            lineWidth = 0.0f;
            breakPos = std::string::npos;
            prev = 0;
            continue;
        }

        const size_t start = cursor;
        const u32 cp = DecodeUtf8(text, length, cursor);
        f32 advance = AdvanceOf(cp);
        if (prev != 0) advance += Kerning(prev, cp);

        if (lineWidth + advance > maxWidth && start > lineStart) {
            if (breakPos != std::string::npos && breakPos > lineStart) {
                // Word wrap: break after the last space we saw.
                lines.emplace_back(lineStart, breakPos - lineStart);
                lineStart = breakPos;
            } else {
                // Unbreakable run: hard-split so we always make progress.
                lines.emplace_back(lineStart, start - lineStart);
                lineStart = start;
            }
            cursor = lineStart;
            lineWidth = 0.0f;
            breakPos = std::string::npos;
            prev = 0;
            continue;  // re-read the codepoint that did not fit
        }

        lineWidth += advance;
        prev = cp;
        if (cp == ' ' || cp == '\t') breakPos = cursor;
    }

    lines.emplace_back(lineStart, length - lineStart);
    return static_cast<size_t>(lines.size());
}

// ---------------------------------------------------------------------------
// FontManager
// ---------------------------------------------------------------------------

FontManager::FontManager() = default;
FontManager::~FontManager() = default;

bool FontManager::Initialize(const char* familyDir, f32 dpiScale) {
    familyDir_ = familyDir != nullptr ? familyDir : "";
    dpiScale_ = Max(0.25f, dpiScale);
    appliedDpi_ = dpiScale_;
    dpiDirty_ = false;

    regularTtf_.clear();
    semiboldTtf_.clear();
    monoTtf_.clear();

    const size_t familyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);
    bool loaded = false;
    for (size_t f = 0; f < familyCount && !loaded; ++f) {
        if (FindAndLoad(kFamilies[f].regular, familyDir_.c_str(), regularTtf_)) loaded = true;
    }
    if (!loaded) {
        ARGON_WARN("Font", "no usable system font found - ArgonUI text will not render");
        return false;
    }

    for (size_t f = 0; f < familyCount; ++f) {
        if (FindAndLoad(kFamilies[f].semibold, familyDir_.c_str(), semiboldTtf_)) break;
    }
    for (size_t f = 0; f < familyCount; ++f) {
        if (FindAndLoad(kFamilies[f].mono, familyDir_.c_str(), monoTtf_)) break;
    }

    initialized_ = true;
    return true;
}

void FontManager::SetDpiScale(f32 dpiScale) {
    const f32 s = Max(0.25f, dpiScale);
    if (NearlyEqual(s, dpiScale_)) return;
    dpiScale_ = s;
    dpiDirty_ = true;
}

void FontManager::ApplyDpiIfDirty() {
    if (!dpiDirty_) return;
    dpiDirty_ = false;
    appliedDpi_ = dpiScale_;

    // Re-bake every atlas at the new scale. Cheap: there are at most a dozen
    // sizes and the TTF data is already resident.
    for (auto& e : entries_) {
        if (e == nullptr || e->font == nullptr) continue;
        FontDesc d;
        d.pixelSize = e->logicalSize;
        d.dpiScale = appliedDpi_;
        d.oversample = 1.5f;
        d.atlasWidth = 512;
        d.atlasHeight = 512;

        const std::vector<u8>& ttf = e->semibold ? semiboldTtf_
                                   : (e->mono ? monoTtf_ : regularTtf_);
        const std::vector<u8>& src = ttf.empty() ? regularTtf_ : ttf;
        if (!src.empty()) {
            e->font->LoadFromMemory(src.data(), src.size(), d);
        }
    }
}

Font* FontManager::Find(f32 logicalSize, int face) {
    ApplyDpiIfDirty();
    // Quantise so 13.9 and 14.0 share an atlas.
    const f32 q = std::floor(logicalSize * 2.0f + 0.5f) * 0.5f;
    for (auto& e : entries_) {
        if (e != nullptr && NearlyEqual(e->logicalSize, q) && e->semibold == (face == FaceSemibold) &&
            e->mono == (face == FaceMono)) {
            return e->font.get();
        }
    }
    return nullptr;
}

Font* FontManager::Create(f32 logicalSize, int face) {
    if (!initialized_) return nullptr;

    const std::vector<u8>& ttf =
        (face == FaceSemibold) ? semiboldTtf_ : ((face == FaceMono) ? monoTtf_ : regularTtf_);
    const std::vector<u8>& src = ttf.empty() ? regularTtf_ : ttf;
    if (src.empty()) return nullptr;

    FontDesc d;
    d.pixelSize = logicalSize;
    d.dpiScale = appliedDpi_;
    d.oversample = 1.5f;
    // Small sizes need far less atlas space; big display text needs more.
    const u32 side = logicalSize <= 13.0f ? 256u : (logicalSize <= 20.0f ? 512u : 1024u);
    d.atlasWidth = side;
    d.atlasHeight = side;

    auto entry = std::make_unique<Entry>();
    entry->logicalSize = std::floor(logicalSize * 2.0f + 0.5f) * 0.5f;
    entry->semibold = (face == FaceSemibold);
    entry->mono = (face == FaceMono);
    entry->font = std::make_unique<Font>();
    if (!entry->font->LoadFromMemory(src.data(), src.size(), d)) {
        return nullptr;
    }
    Font* raw = entry->font.get();
    entries_.push_back(std::move(entry));
    return raw;
}

Font* FontManager::AtSize(f32 logicalSize) {
    if (Font* f = Find(logicalSize, FaceRegular)) return f;
    return Create(Max(1.0f, logicalSize), FaceRegular);
}

Font* FontManager::SemiboldAtSize(f32 logicalSize) {
    if (Font* f = Find(logicalSize, FaceSemibold)) return f;
    if (Font* f = Create(Max(1.0f, logicalSize), FaceSemibold)) return f;
    // No bold face available on this system: fall back to the regular one.
    return AtSize(logicalSize);
}

Font* FontManager::MonoAtSize(f32 logicalSize) {
    if (Font* f = Find(logicalSize, FaceMono)) return f;
    if (Font* f = Create(Max(1.0f, logicalSize), FaceMono)) return f;
    return AtSize(logicalSize);
}

bool FontManager::SyncTextures(IArgonBackend& backend) {
    bool changed = false;
    for (auto& e : entries_) {
        if (e == nullptr || e->font == nullptr || !e->font->Valid()) continue;
        if (e->textureId != 0 && !e->font->AtlasDirty()) continue;

        TextureDesc td;
        td.width = e->font->AtlasWidth();
        td.height = e->font->AtlasHeight();
        td.format = e->font->AtlasFormat();
        td.filterLinear = true;

        if (e->textureId == 0) {
            e->textureId = backend.CreateTexture(td, e->font->AtlasPixels());
        } else {
            if (!backend.UpdateTexture(e->textureId, td, e->font->AtlasPixels())) {
                ARGON_WARN("Font", "glyph atlas update failed for size %.1f", e->logicalSize);
                continue;
            }
        }
        e->font->MarkAtlasClean();
        changed = true;
    }
    return changed;
}

void FontManager::ReleaseTextures(IArgonBackend& backend) {
    for (auto& e : entries_) {
        if (e == nullptr) continue;
        if (e->textureId != 0) {
            backend.DestroyTexture(e->textureId);
            e->textureId = 0;
        }
    }
}

void FontManager::Invalidate() {
    for (auto& e : entries_) {
        if (e != nullptr && e->font != nullptr) e->font->ClearGlyphCache();
    }
}

u32 FontManager::TextureIdOf(const Font* font) const {
    for (const auto& e : entries_) {
        if (e != nullptr && e->font.get() == font) return e->textureId;
    }
    return 0;
}

} // namespace argon::ui