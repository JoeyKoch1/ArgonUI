# DPI and scaling

ArgonUI does not scale your UI. It bakes text at the size you ask for, which is
the actual problem, and the rest follows from there.

## What the scale factor does

```cpp
FrameState fs;
fs.dpiScale = 96.0f / monitorDpi;   // or 1.0f on Windows
ctx.NewFrame(fs);
```

`dpiScale` multiplies `FontDesc::pixelSize` when an atlas is baked. A 14 px font
at 150% becomes a 21 px atlas. That is what keeps text sharp: there is no
upscale anywhere in the pipeline.

Everything else is in pixels already. A 34 px button is 34 physical pixels at
any scale, which is usually what you want in a game overlay, where the menu
should occupy the same fraction of the screen regardless of density.

If you want the whole UI to scale instead, multiply your layout numbers rather
than the font sizes. The style vars are a convenient single place to do it:

```cpp
const f32 s = ctx.Frame().dpiScale;
ctx.PushStyleVar(StyleVarId::ButtonHeight, 34.0f * s);
ctx.PushStyleVar(StyleVarId::Gap, 8.0f * s);
// ... widgets ...
ctx.PopStyleVar(2);
```

Do not scale font size and metrics separately, or the text will not fit its
buttons.

## Changing DPI at runtime

Drag a window between monitors with different density and the scale changes.

```cpp
float dpi = GetDpiForWindow(hwnd) / 96.0f;
if (!NearlyEqual(dpi, lastDpi, 0.01f)) {
    lastDpi = dpi;
    ctx.GetStyle().MutableTheme().metrics.windowPadding *= (dpi / lastDpi);
}
```

ArgonUI handles the font side automatically. `NewFrame` calls
`FontManager::SetDpiScale`, which marks the family dirty, and the next
`FontAt` re-bakes every atlas at the new scale. Metrics do not move, because
ArgonUI has no notion of density-scaled layout beyond fonts.

## Report the real value

```cpp
fs.dpiScale = (float)GetDpiForWindow(hwnd) / 96.0f;
```

Do not leave it at 1.0 on a high-density display if you care about text
quality. Nothing crashes, the text is just baked small and upscaled.

## Mouse position

Input and display size must be in the same space. With a swapchain that is
client pixels, a raw client-space mouse position is already correct. If you
render into a texture at a different resolution than your window, scale the
position to match or every hit test is wrong:

```cpp
const f32 sx = textureWidth  / (float)clientWidth;
const f32 sy = textureHeight / (float)clientHeight;
io.mousePos = Vec2(rawX * sx, rawY * sy);
fs.displaySize = { textureWidth, textureHeight };
```

## Anti-aliasing and scale

The shader derives coverage from `fwidth` of the signed distance, which is one
device pixel wide at any transform. Nothing to tune per DPI.

Leave hardware MSAA off. The analytic AA and coverage AA would both apply and
edges would look soft. If your swapchain is unavoidably multisampled, that is
visible and there is no per-widget fix.

## See also

- [Text and fonts](text.md)
- [Performance](../guides/performance.md)