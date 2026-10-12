# Fonts

A `Font` is a baked glyph atlas plus a per-codepoint metrics cache. Glyphs are
rasterised on demand with stb_truetype and shelf-packed into one texture that
the backend uploads only when it actually changed. `FontManager` owns every
font the UI needs, one atlas per distinct pixel size, created lazily.

This page documents [`include/argonui/Text/Font.h`](../../include/argonui/Text/Font.h).

## The atlas

The atlas texture is R8 (raw glyph coverage), not RGBA8. That is a quarter of
the VRAM an RGBA8 atlas would cost, and the fragment path is identical to the
one used for images and flat fills because the shader samples the red channel
into the shape's alpha.

Because the atlas holds straight coverage, it follows the same texture rule as
everything else: upload the bytes verbatim, never premultiply.

## Glyph baking is lazy and per size

`FontManager` keeps one baked atlas per distinct pixel size rather than scaling
a single atlas. That is why ArgonUI stays crisp at 125% / 150% / 200% DPI and
across the type scale in the reference UI (11 / 12 / 14 / 18 / 24 px).

`FontManager::AtSize` creates the atlas on first use, so asking for a new pixel
size costs a bake. Entries are heap allocated and never move, so a `Font*` stays
valid for the manager's lifetime.

## Uploading atlases

`Font::AtlasDirty` tells you whether new glyphs have been baked since the last
upload. Call `FontManager::SyncTextures` once per frame and it uploads only the
atlases that actually changed, returning true if anything changed. A font that
was loaded but never drawn has nothing to upload, so the first `SyncTextures`
after a load is where the bytes move.

{% hint style="info" %}
A freshly loaded font pre-bakes ASCII (codepoints 32..126), so the atlas already
has content and is marked dirty before you ever draw a string. Glyphs outside
ASCII are baked on demand the first time `GetGlyph` sees them, which flips
`AtlasDirty` again and will be picked up by the next `SyncTextures`.
{% endhint %}

{% hint style="warning" %}
If you never call `SyncTextures`, `Font::TextureIdOf` keeps returning 0 and text
does not draw: `Context::AddText` looks the atlas up by handle and silently
skips the quads when the handle is 0. Measurement still works.
{% endhint %}

## UTF-8

ArgonUI text is UTF-8 throughout. The `DecodeUtf8` overloads read a codepoint and
advance an index; `EncodeUtf8` writes one. Decoding is total: a malformed
sequence yields U+FFFD and resynchronises on the next byte rather than
throwing or reading out of bounds.

### `DecodeUtf8`

```cpp
u32 DecodeUtf8(const char* text, size_t length, size_t& i);
u32 DecodeUtf8(const std::string& text, size_t& i);
```

Decodes one UTF-8 codepoint starting at `i` and advances `i` past it.

**Returns:** the codepoint, or 0 when `i` is already at `length`. Malformed
bytes decode to U+FFFD (0xFFFD) and still consume a byte so the caller makes
progress.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | Buffer to decode. |
| `length` | `size_t` | | Length of `text` in bytes. |
| `i` | `size_t&` | | In: byte offset to read. Out: offset just past the codepoint. |

The `std::string` overload forwards to the pointer overload with the string's
own size, so a string containing an embedded null simply decodes up to its real
length.

{% hint style="info" %}
Overlong encodings, surrogates and codepoints above U+10FFFF all collapse to
U+FFFD. A zero byte decodes to codepoint 0, which is indistinguishable from the
end-of-buffer result; stop on `length`, not on a zero codepoint.
{% endhint %}

**Example**

```cpp
size_t i = 0;
while (i < text.size()) {
    const u32 cp = DecodeUtf8(text, i);
    handleCodepoint(cp);
}
```

### `EncodeUtf8`

```cpp
size_t EncodeUtf8(u32 codepoint, char* out);
```

Appends a codepoint as UTF-8.

**Returns:** the number of bytes written (1 to 4), or 0 if `out` is null.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `codepoint` | `u32` | | The codepoint to encode. |
| `out` | `char*` | | Destination buffer, at least 4 bytes. |

## Data types

### `Glyph`

Per-glyph baked data. Obtain one from `Font::GetGlyph`, never construct it.

| Field | Type | Meaning |
|---|---|---|
| `width` | `f32` | Bitmap width in pixels. |
| `height` | `f32` | Bitmap height in pixels. |
| `bearingX` | `f32` | Pen-relative bitmap left offset in pixels (screen x). |
| `bearingY` | `f32` | Pen-relative bitmap top offset in pixels, screen space with Y growing downward. Negative above the baseline, positive for descenders. |
| `advanceX` | `f32` | Pen advance in pixels. |
| `advanceY` | `f32` | Pen advance in pixels, vertical. Always 0 for horizontal text. |
| `uv` | `Rect` | Atlas UV in [0,1]. Empty for whitespace. |
| `baked` | `bool` | True once the glyph has been rasterised. |
| `isWhitespace` | `bool` | True when the glyph renders nothing (space, tab, zero-width joiner, any glyph with a zero advance). |

Draw the bitmap at `(pen.x + bearingX, pen.y + bearingY)` where `pen.y` is the
baseline. Then advance the pen by `advanceX`.

{% hint style="info" %}
`Context::AddText` snaps the glyph's x origin to a whole pixel, because the
atlas is sampled with a linear filter and a fractional origin would smear it.
The y origin is left fractional.
{% endhint %}

### `FontDesc`

Options for `Font::LoadFromMemory`, `LoadFromFile` and `FontManager`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `pixelSize` | `f32` | `15.0f` | Em size in pixels. |
| `dpiScale` | `f32` | `1.0f` | Applied on top of `pixelSize`. |
| `lineGap` | `f32` | `1.0f` | Extra pixels added between lines. |
| `oversample` | `f32` | `1.25f` | Rasterisation oversample. 1 is native; 1.5 to 2 gives noticeably cleaner diagonals at the cost of atlas memory. |
| `hintLevel` | `f32` | `1.0f` | 0 = unhinted (smoother, blurrier small), 1 = normal. |
| `atlasWidth` | `u32` | `1024` | Atlas width in pixels, clamped to at least 64. |
| `atlasHeight` | `u32` | `1024` | Atlas height in pixels, clamped to at least 64. |
| `italic` | `bool` | `false` | Select the italic face where the loader offers one. |
| `bold` | `bool` | `false` | Select the bold face where the loader offers one. |

{% hint style="info" %}
`FontManager` does not use these defaults. It builds its own `FontDesc` with
`oversample = 1.5f` and an atlas sized by the logical size: 256px for sizes at
or below 13, 512px up to 20, 1024px above that. A font loaded by hand uses the
1024x1024 default, which is generous for a single atlas but wasteful for the
dozen a manager holds.
{% endhint %}

## Font

`Font` is move-only. It is non-copyable so the atlas's internal glyph pointers
cannot outlive it.

```cpp
Font(Font&&) noexcept;
Font& operator=(Font&&) noexcept;
```

Move construction and move assignment transfer the atlas and the TTF bytes. A
moved-from `Font` is empty: `Valid()` returns false and `GetGlyph` returns
nullptr.

### `Font::LoadFromMemory`

```cpp
bool LoadFromMemory(const void* data, size_t size, const FontDesc& desc);
```

Takes ownership of a copy of `data` and bakes the font.

**Returns:** true if the data parsed as a TrueType face.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `data` | `const void*` | | Start of the font file. Copied. |
| `size` | `size_t` | | Length of `data` in bytes. |
| `desc` | `const FontDesc&` | | Size, oversample and atlas configuration. |

The resolved pixel size is `max(1, pixelSize * max(0.1, dpiScale))`. After a
successful load the font pre-bakes ASCII, marks the atlas dirty, and
`Valid()` returns true.

### `Font::LoadFromFile`

```cpp
bool LoadFromFile(const char* path, const FontDesc& desc);
```

Reads a font file and loads it.

**Returns:** true on success. False if the file could not be read or did not
parse.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `path` | `const char*` | | Path to a `.ttf` or `.otf`. |
| `desc` | `const FontDesc&` | | Size, oversample and atlas configuration. |

### `Font::LoadSystemDefault`

```cpp
static bool LoadSystemDefault(Font& out, const FontDesc& desc,
                              const char* familyHint = nullptr);
```

Walks a small, well-known list of system fonts and loads the first that
exists. The list covers Inter, the Windows UI font family and Roboto, each with
per-role regular, semibold and mono candidates.

**Returns:** true if a face was found and loaded into `out`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `out` | `Font&` | | Destination font. Its previous contents are replaced. |
| `desc` | `const FontDesc&` | | Size, oversample and atlas configuration. |
| `familyHint` | `const char*` | `nullptr` | An explicit path. When it loads, it wins over every candidate. |

{% hint style="warning" %}
`familyHint` is a file path, not a family name. Passing `"Segoe UI"` will not
resolve; pass `"C:/Windows/Fonts/segoeui.ttf"`. A hint that fails to read falls
through to the candidate list rather than failing outright.
{% endhint %}

### `Font::Clear`

```cpp
void Clear();
```

Unloads the font, drops every cached glyph, and resets the atlas and metrics to
defaults. The object is reusable and `Valid()` returns false afterwards.

**Returns:** nothing.

### `Font::Valid`

```cpp
bool Valid() const { return valid_; }
```

**Returns:** true if a face is loaded and metrics are available. A font that
fails to load, or one that was cleared or moved from, returns false. Every
measurement call returns a zero-ish result in that state.

### Metrics

```cpp
f32 PixelSize() const { return pixelSize_; }
f32 LineHeight() const { return lineHeight_; }
f32 Ascent() const { return ascent_; }
f32 Descent() const { return descent_; }
```

**Returns:** the resolved pixel size; the line height in pixels (ascent +
descent + line gap + `FontDesc::lineGap`); and the ascent and descent distances
from the baseline. All in pixels.

A `pen.y` passed to `Context::AddText` is a baseline, so add `Ascent()` to a
line's top edge to place text inside it.

### Atlas access

```cpp
bool AtlasDirty() const { return atlasDirty_; }
void MarkAtlasClean() { atlasDirty_ = false; }
const u8* AtlasPixels() const { return atlas_.data(); }
u32 AtlasWidth() const { return atlasW_; }
u32 AtlasHeight() const { return atlasH_; }
TextureFormat AtlasFormat() const { return TextureFormat::R8; }
```

`AtlasDirty` reports whether glyphs have been baked since the last upload.
`MarkAtlasClean` clears the flag; `FontManager::SyncTextures` calls it for you
after an upload. The remaining four hand you the raw bytes and geometry for
`CreateTexture` / `UpdateTexture`.

**Returns:** the dirty flag; nothing; a pointer to `width * height` bytes of
coverage; the atlas dimensions; and always `TextureFormat::R8`.

The bytes are single-channel coverage, row-major, tight stride. `AtlasHeight`
can exceed `FontDesc::atlasHeight` after growth: a full atlas doubles its height
(up to 8192) and keeps every existing glyph's UV valid, so anything already
emitted this frame stays correct. Re-read the height after any call that can
bake a glyph.

### `Font::ClearGlyphCache`

```cpp
void ClearGlyphCache();
```

Releases cached glyphs and rebuilds the atlas from scratch, leaving the TTF
data and the metrics intact. Used on a large DPI or style change.

**Returns:** nothing.

Every previously handed-out `const Glyph*` dangles after this call.

### `Font::GetGlyph`

```cpp
const Glyph* GetGlyph(u32 codepoint);
```

Returns the glyph for a codepoint, baking it into the atlas on first request.

**Returns:** a pointer to the glyph, valid until `ClearGlyphCache` or `Clear`.
Null only when the font is not valid.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `codepoint` | `u32` | | A Unicode codepoint. |

A codepoint the face does not contain resolves to the fallback glyph (`?`) with
a zero `advanceX`, so layout keeps going without a gap. Glyphs that render
nothing (space, tab, a zero-width joiner, anything with a zero advance) come back
with `isWhitespace` true and no atlas space consumed.

### Measurement

```cpp
Vec2 MeasureText(const char* text, size_t length) const;
Vec2 MeasureText(const std::string& text) const;
```

Measures a possibly multi-line string.

**Returns:** `(widest line width, total height)`. Width is the widest line; height
is `lineHeight` times the line count. Honours `\n` and applies kerning between
adjacent pairs.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | String to measure. |
| `length` | `size_t` | | Byte length of `text`. |

### `Font::MeasureLine`

```cpp
f32 MeasureLine(const char* text, size_t length) const;
```

Width of a single line, with no wrapping.

**Returns:** width in pixels, or 0 for an invalid font, a null string or zero
length.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | String to measure. |
| `length` | `size_t` | | Byte length of `text`. |

### `Font::AdvanceOf`

```cpp
f32 AdvanceOf(u32 codepoint) const;
```

Pen advance for one codepoint.

**Returns:** advance in pixels. Glyphs already in the cache return their baked
advance; others are measured straight from the face without baking, so this is
safe to call from `const` code and from layout passes.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `codepoint` | `u32` | | A Unicode codepoint. |

### `Font::Ellipsize`

```cpp
size_t Ellipsize(const char* text, size_t length, f32 maxWidth,
                 const char* ellipsis = "...") const;
```

Finds the longest prefix of `text` that fits in `maxWidth` once the ellipsis is
appended.

**Returns:** the number of bytes to keep from the front of `text`, excluding the
terminator. When the whole string already fits it returns `length` unchanged and
no ellipsis is needed. When nothing fits alongside the ellipsis it returns the
length of the ellipsis itself, so the caller emits the ellipsis alone. Never
bakes or returns a suffix.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | String to truncate. |
| `length` | `size_t` | | Byte length of `text`. |
| `maxWidth` | `f32` | | Budget in pixels, including the ellipsis. |
| `ellipsis` | `const char*` | `"..."` | Suffix to reserve room for. May be null. |

{% hint style="info" %}
The returned count is a prefix length, not a truncation marker. The caller
measures `MeasureLine(text, n)` and draws that many bytes, then draws the
ellipsis itself.
{% endhint %}

### `Font::WrapText`

```cpp
size_t WrapText(const char* text, size_t length, f32 maxWidth,
                std::vector<std::pair<size_t, size_t>>& lines) const;
```

Greedy word wrap into a caller-provided vector.

**Returns:** the number of lines written to `lines`, each a `{offset, length}`
byte pair into `text`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | String to wrap. |
| `length` | `size_t` | | Byte length of `text`. |
| `maxWidth` | `f32` | | Line width budget in pixels. |
| `lines` | `std::vector<std::pair<size_t, size_t>>&` | | Output, cleared first. |

The vector is cleared before use, so pass it by reference and read it after the
call. Lines break at the last space seen before the overflow; a run with no
break opportunity is hard-split so the loop always makes progress. `\n` forces a
line break. A line can exceed `maxWidth` only when a single word is longer than
`maxWidth`.

### `Font::Kerning`

```cpp
f32 Kerning(u32 first, u32 second) const;
```

Kerning adjustment between two adjacent codepoints.

**Returns:** the adjustment in pixels, normally negative for a tighter pair.
Zero when either codepoint is absent from the face, when the font is invalid,
or when the face has no kerning data.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `first` | `u32` | | The earlier codepoint. |
| `second` | `u32` | | The following codepoint. |

## FontManager

`FontManager` owns every font the UI needs. Each distinct pixel size gets its own
baked atlas (R8, typically 512x512 = 256 KB), created lazily the first time a
widget asks for that size. Entries are heap allocated and never move, so a
`Font*` handed out once stays valid for the lifetime of the manager.

### `FontManager::Initialize`

```cpp
bool Initialize(const char* familyDir = nullptr, f32 dpiScale = 1.0f);
```

Builds the default family. A semibold and a mono face are loaded alongside the
regular one; each falls back to the regular face when the system has no
matching file.

**Returns:** true if a regular face was found. False leaves the manager
uninitialized, and every `AtSize` then returns nullptr, so no text renders.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `familyDir` | `const char*` | `nullptr` | A directory of TTFs. Null locates a system font automatically. |
| `dpiScale` | `f32` | `1.0f` | Initial DPI scale, clamped to at least 0.25. |

`familyDir` is joined to the *relative* candidate paths (`Fonts/Inter-Regular.ttf`
and friends) with a slash; an absolute candidate already containing a drive
letter or leading slash is used as-is.

{% hint style="warning" %}
When no regular font is found the manager logs a warning and returns false, but
text simply never draws afterwards rather than crashing. If your build ships no
fonts and the machine has none in the expected places, pass `familyDir` and
confirm it returns true.
{% endhint %}

### `FontManager::SetDpiScale`

```cpp
void SetDpiScale(f32 dpiScale);
```

Marks the family dirty. Atlases are re-baked at the next `AtSize` call.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `dpiScale` | `f32` | | New scale, clamped to at least 0.25. |

Cheap to call every frame: a value within `NearlyEqual` tolerance of the current
one is ignored, and the re-bake only happens when the scale actually changed.
The re-bake reloads every existing entry from the resident TTF data at
`oversample = 1.5f` with a 512x512 atlas, then re-uploads on the next
`SyncTextures`.

### `FontManager::DpiScale`

```cpp
f32 DpiScale() const { return dpiScale_; }
```

**Returns:** the requested scale. This is the pending value; the value actually
baked into the atlases lags until the next `AtSize` call.

### `FontManager::AtSize`

```cpp
Font* AtSize(f32 logicalSize);
```

Font for a logical pixel size in the regular face, creating the atlas on first
use.

**Returns:** a `Font*` owned by the manager and valid for its lifetime, or
nullptr if the manager is not initialized.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `logicalSize` | `f32` | | Logical size before DPI scaling, clamped to at least 1. |

{% hint style="warning" %}
The first call for a size pays for a full ASCII pre-bake and allocates the
atlas. Asking for a new size every frame (for example from a continuously
animated font size) will bake an atlas per distinct half-step forever. Sizes are
quantised to the nearest 0.5, so 13.9 and 14.0 share one atlas, but 14.0 and
14.5 do not.
{% endhint %}

### `FontManager::SemiboldAtSize`

```cpp
Font* SemiboldAtSize(f32 logicalSize);
```

As `AtSize`, in the semibold face.

**Returns:** a `Font*` owned by the manager, or nullptr if uninitialized.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `logicalSize` | `f32` | | Logical size before DPI scaling. |

If the system has no semibold face the call falls back to the regular font at
the same size, so the return is never null for an initialized manager.

### `FontManager::MonoAtSize`

```cpp
Font* MonoAtSize(f32 logicalSize);
```

As `AtSize`, in the monospaced face.

**Returns:** a `Font*` owned by the manager, or nullptr if uninitialized.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `logicalSize` | `f32` | | Logical size before DPI scaling. |

Falls back to the regular face when no monospaced font is available.

### Body-size conveniences

```cpp
Font* Regular() { return AtSize(FontManager::kSizeBody); }
Font* Semibold() { return SemiboldAtSize(FontManager::kSizeBody); }
Font* Mono() { return MonoAtSize(FontManager::kSizeBody); }
```

**Returns:** the regular, semibold or mono font at `kSizeBody`, or nullptr if
uninitialized.

The default weights used across the reference UI are compile-time constants:

| Constant | Value | Use |
|---|---|---|
| `FontManager::kSizeBody` | `14.0f` | Body text. |
| `FontManager::kSizeSmall` | `12.0f` | Secondary text. |
| `FontManager::kSizeTiny` | `11.0f` | Dense labels. |
| `FontManager::kSizeTitle` | `18.0f` | Section titles. |
| `FontManager::kSizeDisplay` | `24.0f` | Display headings. |

### `FontManager::SyncTextures`

```cpp
bool SyncTextures(class IArgonBackend& backend);
```

Creates or updates the GPU texture for every atlas that is new or dirty.

**Returns:** true if anything was uploaded.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `backend` | `class IArgonBackend&` | | Backend that owns the texture handles. |

Entries that already have a handle and a clean atlas are skipped, so a steady
state costs one flag check per font. A failed `UpdateTexture` logs a warning,
leaves the atlas marked dirty and retries next frame.

Call it once per frame. It creates textures with `filterLinear = true` and the
font's own `R8` format.

### `FontManager::ReleaseTextures`

```cpp
void ReleaseTextures(class IArgonBackend& backend);
```

Destroys the GPU textures but keeps the CPU-side atlases.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `backend` | `class IArgonBackend&` | | Backend that owns the texture handles. |

Use it when a device is lost and will be recreated. The next `SyncTextures`
re-creates the textures from the retained atlases, which is far cheaper than
re-baking.

{% hint style="warning" %}
Pass the backend that created the textures. Destroying a texture on a different
backend, or after that backend's device is gone, is undefined.
{% endhint %}

### `FontManager::Invalidate`

```cpp
void Invalidate();
```

Drops every glyph cache, forcing a full re-bake at the next `SyncTextures`.

**Returns:** nothing.

Use it on a large DPI or style change. The glyph entries are cleared but the
atlas storage stays allocated, so the re-bake does not churn memory. Any
`const Glyph*` you are still holding dangles.

### `FontManager::TextureIdOf`

```cpp
u32 TextureIdOf(const Font* font) const;
```

Looks up the GPU texture handle for a font.

**Returns:** the handle `SyncTextures` created, or 0 when the font is unknown or
has not been uploaded yet.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `font` | `const Font*` | | A font obtained from this manager. |

{% hint style="info" %}
A handle of 0 means text will not draw. That is the normal state before the first
`SyncTextures`, and it is also what you get from a `Font` the manager does not
own. `Context::AddText` measures correctly in either case and simply emits no
quads.
{% endhint %}

## See also

- [Text and fonts](../concepts/text.md) - how text flows into the draw list.
- [Style](style.md) - the `TextSize*` vars that pick a font size.
- [Backend interface](backend.md) - `CreateTexture` and `UpdateTexture`.
- [Context](context.md) - `AddText` and the per-frame font sync.