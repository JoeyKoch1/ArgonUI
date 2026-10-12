# Text and fonts

Text is where an in-game menu spends most of its pixels, so it gets more
attention than it does in most UI libraries.

## One atlas per pixel size

ArgonUI bakes glyphs with stb_truetype into a shelf-packed atlas and uploads it
only when it actually changed. The important decision is that each pixel size
gets its own atlas rather than scaling a shared one.

Scaling a 14 px atlas up to 18 px gives you blurry text at 150% display scaling.
Baking a second atlas at 18 px gives you sharp text. Since a menu uses maybe six
sizes and a glyph is a few hundred bytes, the cost is small and the result is
correct at any DPI.

```cpp
Font* f = ctx.FontAt(14.0f);          // creates the atlas on first use
Font* b = ctx.SemiboldFontAt(18.0f);  // separate face, separate atlas
Font* m = ctx.MonoFontAt(12.0f);      // numbers, ids, logs
```

Asking for a size bakes it on first use, which costs a frame. Ask for the sizes
you use during the first frame, or accept one slow frame.

## R8, not RGBA8

Font atlases are single channel coverage. `VF_TEX_COVERAGE` tells the shader to
read coverage from `.r` and force the tint to white, which means one glyph
pipeline handles text, images and flat fills.

That is a quarter of the VRAM. An RGBA8 512x512 atlas is 1 MB per size; R8 is
256 KB. Across six sizes that is 1.5 MB rather than 6 MB, which is worth having
in a process that also has the game rendering.

## Uploads

```cpp
// Context::NewFrame does this for you:
fonts_.SyncTextures(*backend_);
```

`Font::AtlasDirty()` is true when a new glyph has been baked. Only dirty atlases
are uploaded. If you drive uploads yourself, call `SyncTextures` once per frame
after `NewFrame` and before any text is drawn.

## Where fonts come from

```cpp
fonts_.Initialize(nullptr);                       // system default
fonts_.Initialize("C:/Assets/Fonts", 1.0f);       // explicit directory
```

ArgonUI looks for Inter, then the Windows UI font (Segoe UI, Arial), then
Roboto, and on Linux for the DejaVu family. An explicit directory wins, then a
system default. When nothing is found it logs a warning and text does not
render, but nothing crashes.

To pin a font, load it yourself:

```cpp
FontDesc d;
d.pixelSize = 14.0f;
d.oversample = 1.5f;
d.atlasWidth = 512;
d.atlasHeight = 512;

Font font;
font.LoadFromFile("Assets/Inter-SemiBold.ttf", d);
```

`oversample` above 1 rasterises larger and area-averages down. It costs atlas
space and improves diagonal quality at small sizes. 1.5 is a reasonable default;
1.0 is fine above 18 px.

## Measuring

```cpp
Vec2 size = font->MeasureText(text, length);   // wraps on \n
f32 width = font->MeasureLine(text, length);   // one line
f32 line  = font->LineHeight();
```

`MeasureText` honours newlines. `MeasureLine` does not, which is what you want
inside a single-line widget.

The context wraps it:

```cpp
f32 w = ctx.CalcTextSizeX("Label");
f32 h = ctx.CalcTextHeight("Label");
```

## Wrapping and ellipsis

```cpp
std::vector<std::pair<size_t, size_t>> lines;
font->WrapText(text, len, maxWidth, lines);    // byte offset, length per line

size_t shown = font->Ellipsize(text, len, maxWidth, "...");
```

`WrapText` breaks on spaces where it can and hard-splits a single word too long
for the line, so it always terminates. `Ellipsize` returns the byte count to
draw, having dropped the rest and accounted for the ellipsis width.

Both return byte offsets into the original string, not new strings, so you draw
with `text + offset` and get a NUL-terminated slice only when the offset is at
the end. For truncated text use the count:

```cpp
size_t n = font->Ellipsize(text, len, maxWidth);
drawCounted(text, n);
```

## Drawing

```cpp
f32 advance = ctx.AddText(pos, text, color, font);
```

`pos` is the **baseline**, not the top-left. That is deliberate: it is the only
origin that does not require recomputing when a font's ascent differs. For a
top-left origin, add the ascent:

```cpp
ctx.AddText(Vec2(x, y + font->Ascent()), text, color, font);
```

The helpers handle that for you:

```cpp
ctx.AddTextCentered(bounds, text, color, font);
ctx.AddTextRightAligned(bounds, text, color, font);
```

Both centre vertically inside `bounds` by default.

## Kerning

```cpp
font->Kerning(prevCp, cp)
```

Applied automatically inside `Context::AddText`. If you measure text yourself
with `MeasureLine`, kerning is included there too, so measurement and drawing
agree.

## UTF-8 and codepoints

ArgonUI takes UTF-8 strings and decodes to UTF-32 internally. `InputState`
carries `inputCodepoints`, which is `std::vector<u32>`, so a platform layer
pushes codepoints directly:

```cpp
// Win32 WM_CHAR
io.inputCodepoints.push_back((argon::ui::u32)c);
```

Text fields insert them with `EncodeUtf8`, which handles surrogate pairs and
caret movement lands on UTF-8 boundaries rather than splitting a codepoint.

## See also

- [DPI and scaling](dpi.md)
- [Fonts reference](../api/font.md)