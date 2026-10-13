# Internal helpers

The shared helpers behind the widget implementations: the hover and press
animation pair every interactive widget uses, the color utilities, the text
ellipsis and measurement shortcut, and the three paint helpers a custom widget
needs in order to look like a built-in one.

Header: [`include/argonui/Widgets/Internal.h`](../../include/argonui/Widgets/Internal.h).

{% hint style="warning" %}
Everything on this page lives in `argon::ui::detail` and is explicitly not part
of the stable API. The names, signatures and semantics can change between
releases without a changelog entry, and the headers say so themselves. Reach
for them when you are writing a widget of your own and ArgonUI has no built-in
equivalent. Application code should be using the
[controls](controls.md), [containers](widgets.md) and [icons](icons.md) pages
instead.
{% endhint %}

## Animation

These three call into the context's animation store. The store seeds a new slot
at its target on first use, so a widget never pops on the frame it first
appears, and every one of them is frame-rate independent: the rate is a damping
constant, not a per-frame fraction.

### `HoverAnim`

```cpp
inline f32 HoverAnim(Context& ctx, u32 id, bool hovered, f32 rate);
```

A smoothly animated 0 to 1 "how hot is this item" value. Every interactive
widget uses it so hover feels identical everywhere. Pass a distinct `id` per
widget, because the slot is keyed by it.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context owning the animation store. |
| `id` | `u32` | | Animation key. Use the widget's own item id. |
| `hovered` | `bool` | | Target state. |
| `rate` | `f32` | | Damping rate, usually `StyleVarId::HoverRate`. |

**Returns:** the current animated value in the range 0 to 1. Blend it into a
color or add it to an offset. Treat it as a float for the whole transition: the
damping only snaps to the target at the end, when it is within a small
tolerance.

{% hint style="info" %}
`HoverAnim` and `PressAnim` share the same store and the same rate parameters,
which is why a hover and a press always land together across the whole UI. Keep
the pair when you write a widget so the timing stays in step.
{% endhint %}

### `PressAnim`

```cpp
inline f32 PressAnim(Context& ctx, u32 id, bool held, f32 rate);
```

The animated counterpart of `HoverAnim` for "the button is down". It stores
under `id ^ 0x5bf03635u`, so it never collides with the hover slot of the same
widget even when both are called with the same id and rate.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context owning the animation store. |
| `id` | `u32` | | Animation key. The slot is offset by a constant. |
| `held` | `bool` | | Target state, usually the widget's `ItemResult::held`. |
| `rate` | `f32` | | Damping rate, usually `StyleVarId::PressRate`. |

**Returns:** the current animated value in the range 0 to 1.

### `ExpandAnim`

```cpp
inline f32 ExpandAnim(Context& ctx, u32 id, bool open);
```

The 0 to 1 expand used by combos, popups and accordions. It passes a duration of
zero to `Context::AnimTo`, so the rate falls back to `StyleVarId::ExpandRate`
rather than to a per-widget constant.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context owning the animation store. |
| `id` | `u32` | | Animation key. |
| `open` | `bool` | | Target state. |

**Returns:** the current animated value in the range 0 to 1, easing with a
cubic out curve.

{% hint style="info" %}
Combo and popup treat a value below 0.05 as fully closed and skip their work
entirely. Copy that threshold rather than testing for zero, or the last few
frames of the close animation cost a full draw.
{% endhint %}

## Color

### `FadeColor`

```cpp
inline Color FadeColor(const Color& c, bool enabled);
```

The color that reads as disabled without losing its hue: the same color at 40
percent of its own alpha. Fading alpha rather than blending toward gray keeps
the hue readable next to the enabled controls around it.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `c` | `const Color&` | | Base color. |
| `enabled` | `bool` | | True returns `c` unchanged. |

**Returns:** the faded or unchanged color.

### `WithAlpha`

```cpp
inline Color WithAlpha(const Color& c, f32 extra = 1.0f);
```

Scales a color's existing alpha by `extra`. Use it to fold a widget's own
opacity into a theme color rather than overwriting it, which is what a state
change such as a press fade needs.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `c` | `const Color&` | | Base color. Its alpha is multiplied, not replaced. |
| `extra` | `f32` | `1.0f` | Scale factor. Above 1 raises the alpha, which is usually a bug. |

**Returns:** the scaled color.

{% hint style="warning" %}
The header comment describes this as applying the current style alpha. It does
not: there is no `Context` parameter. It multiplies the color's own alpha by
`extra`. To fold in the context's style alpha, call it as
`WithAlpha(c, ctx.Alpha())`.
{% endhint %}

## Text

### `EllipsizeWidth`

```cpp
inline f32 EllipsizeWidth(Context& ctx, Font* font, const char* text, f32 maxWidth);
```

The width of `text` after it has been truncated with an ellipsis to fit
`maxWidth`. Call it when the layout needs to reserve exactly as much room as the
string will really occupy, such as sizing a pill around a name that may not fit.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Present for signature symmetry. Not read. |
| `font` | `Font*` | | Font to measure with. Null returns 0. |
| `text` | `const char*` | | String to measure. Null returns 0. |
| `maxWidth` | `f32` | | Available width in pixels. |

**Returns:** the measured width of the ellipsized prefix, or 0 for a null font,
a null string or a string that fits nothing.

### `DrawTextClipped`

```cpp
void DrawTextClipped(Context& ctx, const Rect& bounds, const char* text, const Color& color,
                     Font* font, bool alignCenter, bool alignRight);
```

Draws `text` vertically centered in `bounds`, ellipsized when it does not fit and
clipped by whatever clip rect is on the context's clip stack. The two alignment
flags are independent of the vertical centering, which is always applied: set
`alignCenter` for a centered label, `alignRight` for one pinned to
`bounds.max.x`, or neither for a left-aligned label.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. Its current clip stack applies. |
| `bounds` | `const Rect&` | | Area the text is fitted into. |
| `text` | `const char*` | | String to draw. |
| `color` | `const Color&` | | Text color. |
| `font` | `Font*` | | Font to draw and measure with. |
| `alignCenter` | `bool` | | Center horizontally in `bounds`. |
| `alignRight` | `bool` | | Right-align to `bounds.max.x`. |

**Returns:** nothing, it appends to the draw list.

{% hint style="warning" %}
This function is declared but has no definition anywhere in the tree, so
calling it fails to link. The built-in widgets each carry a private copy of the
same logic. Add a definition before using it, or copy the pattern into your own
widget file.
{% endhint %}

## Painting

### `PaintInteractiveBox`

```cpp
void PaintInteractiveBox(Context& ctx, const Rect& bounds, f32 hover, f32 press,
                         const Color& bgNormal, const Color& bgHovered, const Color& bgActive,
                         const Color& borderNormal, f32 borderWidth, f32 radius, f32 glow = 0.0f,
                         const Color& glowColor = Color(0, 0, 0, 0));
```

The standard interactive surface: background, border and optional glow. The fill
is `bgNormal` blended toward `bgHovered` by `hover` and then toward `bgActive` by
`press`, which is the order the built-in buttons use, so a custom control built
on it cannot drift out of step with the rest of the UI.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `bounds` | `const Rect&` | | Surface rectangle. |
| `hover` | `f32` | | `HoverAnim` value, 0 to 1. |
| `press` | `f32` | | `PressAnim` value, 0 to 1. |
| `bgNormal` | `const Color&` | | Resting fill. |
| `bgHovered` | `const Color&` | | Fill at full hover. |
| `bgActive` | `const Color&` | | Fill at full press. |
| `borderNormal` | `const Color&` | | Border color. There is no separate hover border. |
| `borderWidth` | `f32` | | Border thickness in pixels. 0 draws no border. |
| `radius` | `f32` | | Corner radius in pixels. |
| `glow` | `f32` | `0.0f` | Outer glow spread in pixels. 0 draws no glow. |
| `glowColor` | `const Color&` | `Color(0, 0, 0, 0)` | Glow color. |

**Returns:** nothing, it appends to the draw list.

{% hint style="warning" %}
Declared but not defined anywhere in the tree, so this fails to link as written.
Resolve the colors yourself with `ResolveButton` and paint them with
`Context::AddBox` until a definition exists.
{% endhint %}

### `MeasureButtonLabel`

```cpp
Vec2 MeasureButtonLabel(Context& ctx, const char* label, Icon icon, bool iconRight, f32 paddingX,
                        f32 fontSize, f32 height);
```

Measures a button's label plus its optional icon at the given size, so a custom
button can auto-size the same way `Button` does.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context whose fonts and style are used. |
| `label` | `const char*` | | Label text. Null or empty means no label. |
| `icon` | `Icon` | | `Icon::None` means no icon and no gap. |
| `iconRight` | `bool` | | Whether the icon sits after the label. Affects only horizontal order. |
| `paddingX` | `f32` | | Horizontal padding to add on each side. |
| `fontSize` | `f32` | | Label font size in pixels. |
| `height` | `f32` | | Button height in pixels, which sets the icon size and the label gap. |

**Returns:** the size the button needs, as a `Vec2`.

{% hint style="warning" %}
Declared but not defined anywhere in the tree, so this fails to link as written.
`Button` carries its own private copy of the measurement arithmetic; the pattern
it follows is two paddings plus the label width plus the icon at 56 percent of
the height plus a gap at 26 percent of the height when both are present.
{% endhint %}

**Example**

A minimal custom widget that matches the built-ins: one animated surface, one
item, one line of layout.

```cpp
#include "argonui/Widgets/Internal.h"

using namespace argon::ui;

bool GlowButton(Context& ctx, const char* label, Color accent, bool* enabled) {
    const u32 id = ctx.GetID(label);
    const f32 h = ctx.GetVar(StyleVarId::ButtonHeight);
    Font* font = ctx.FontAt(ctx.GetVar(StyleVarId::TextSize));
    const f32 w = font->MeasureLine(label, std::strlen(label)) +
                  ctx.GetVar(StyleVarId::ButtonPaddingX) * 2.0f;

    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));
    const ItemResult r = ctx.ItemBehavior(id, bounds);

    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const Color bg = Color::LerpC(Color::LerpC(accent, accent.WithAlpha(0.85f), hover),
                                  accent.WithAlpha(0.65f), press);
    ctx.AddBox(bounds, bg, CornerRadius(ctx.GetVar(StyleVarId::ButtonRadius)), 0.0f,
               Color(0, 0, 0, 0), press * 4.0f, ctx.GetColor(StyleColorId::Glow));

    const Color fg = enabled != nullptr && !*enabled
                         ? detail::FadeColor(ctx.GetColor(StyleColorId::OnAccent), false)
                         : ctx.GetColor(StyleColorId::OnAccent);
    ctx.AddTextCentered(bounds, label, fg, font);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();
    return r.clicked && (enabled == nullptr || *enabled);
}
```

## See also

- [Controls](controls.md)
- [Containers, pickers and overlays](widgets.md)
- [Icons](icons.md)
- [Writing a widget](../guides/writing-a-widget.md)
