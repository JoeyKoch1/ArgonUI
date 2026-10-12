// ArgonUI - Widgets/Internal.h
// Helpers shared by the widget implementations. Not part of the public API.
#pragma once

#include "../Context.h"

namespace argon::ui::detail {

/// Smoothly animated 0..1 "how hot is this item" value.
/// Every interactive widget uses it so hover/press feel identical everywhere.
inline f32 HoverAnim(Context& ctx, u32 id, bool hovered, f32 rate) {
    return ctx.AnimFloat(id, hovered ? 1.0f : 0.0f, rate);
}

inline f32 PressAnim(Context& ctx, u32 id, bool held, f32 rate) {
    return ctx.AnimFloat(id ^ 0x5bf03635u, held ? 1.0f : 0.0f, rate);
}

/// 0..1 expand used by combos, popups and accordions.
inline f32 ExpandAnim(Context& ctx, u32 id, bool open) {
    return ctx.AnimTo(id, open ? 1.0f : 0.0f, 0.0f);
}

/// Colour that reads as "disabled" without losing its hue.
inline Color FadeColor(const Color& c, bool enabled) {
    return enabled ? c : c.WithAlpha(c.a * 0.4f);
}

/// Applies the current style alpha to a colour.
inline Color WithAlpha(const Color& c, f32 extra = 1.0f) {
    return c.WithAlpha(c.a * extra);
}

/// Width of the text, with an ellipsis, clipped to `maxWidth`.
inline f32 EllipsizeWidth(Context& ctx, Font* font, const char* text, f32 maxWidth) {
    if (font == nullptr || text == nullptr) return 0.0f;
    const size_t n = font->Ellipsize(text, std::strlen(text), maxWidth);
    return font->MeasureLine(text, n);
}

/// Draws `text` vertically centred in `bounds`, clipped to `bounds` by the
/// caller's clip stack, ellipsised when it does not fit.
void DrawTextClipped(Context& ctx, const Rect& bounds, const char* text, const Color& color,
                     Font* font, bool alignCenter, bool alignRight);

/// Standard text button body: bg, border, optional glow. Returns nothing; the
/// caller has already resolved the colour set.
void PaintInteractiveBox(Context& ctx, const Rect& bounds, f32 hover, f32 press,
                         const Color& bgNormal, const Color& bgHovered, const Color& bgActive,
                         const Color& borderNormal, f32 borderWidth, f32 radius, f32 glow = 0.0f,
                         const Color& glowColor = Color(0, 0, 0, 0));

/// Measure a button label + optional icon at the current style.
Vec2 MeasureButtonLabel(Context& ctx, const char* label, Icon icon, bool iconRight, f32 paddingX,
                        f32 fontSize, f32 height);

} // namespace argon::ui::detail