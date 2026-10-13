#include "argonui/Widgets/Controls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace argon::ui {
// Shared helpers

enum class TextAlign { Left, Center, Right };

/// Draws `text` inside `bounds`. The baseline is derived from the font's own
/// ascent so every widget aligns the same way without recomputing it.
static f32 DrawLabel(Context& ctx, const Rect& bounds, const char* text, Font* font,
                     const Color& color, TextAlign align) {
    if (font == nullptr || text == nullptr || *text == '\0') return 0.0f;
    const size_t len = std::strlen(text);
    const size_t shown = font->Ellipsize(text, len, bounds.Width());
    if (shown == 0) return 0.0f;

    const f32 w = font->MeasureLine(text, shown);
    f32 x = bounds.min.x;
    if (align == TextAlign::Center) x = bounds.GetCenter().x - w * 0.5f;
    else if (align == TextAlign::Right) x = bounds.max.x - w;

    const f32 y = bounds.min.y + (bounds.Height() - font->LineHeight()) * 0.5f + font->Ascent();
    ctx.AddText(Vec2(x, y), text, color, font);
    return w;
}

static f32 TextWidth(Font* font, const char* text) {
    if (font == nullptr || text == nullptr) return 0.0f;
    return font->MeasureLine(text, std::strlen(text));
}

/// Re-resolves a hover colour set: normal -> hovered -> active, blended.
static Color ResolveInteractive(const Color& normal, const Color& hovered, const Color& active,
                                f32 hover, f32 press) {
    Color c = Color::LerpC(normal, hovered, hover);
    return Color::LerpC(c, active, press);
}

// Button

ButtonColors ResolveButton(ButtonKind kind, const Style& style) {
    ButtonColors c{};
    const Color card = style.GetColor(StyleColorId::CardBg);
    const Color cardHover = style.GetColor(StyleColorId::CardBgHover);
    const Color field = style.GetColor(StyleColorId::FieldBg);
    const Color fieldHover = style.GetColor(StyleColorId::FieldBgHover);
    const Color border = style.GetColor(StyleColorId::CardBorder);
    const Color accent = style.GetColor(StyleColorId::Accent);
    const Color danger = style.GetColor(StyleColorId::Danger);
    const Color warning = style.GetColor(StyleColorId::Warning);

    c.radius = style.GetVar(StyleVarId::ButtonRadius);
    c.height = style.GetVar(StyleVarId::ButtonHeight);
    c.paddingX = style.GetVar(StyleVarId::ButtonPaddingX);
    c.fgDisabled = style.GetColor(StyleColorId::TextDisabled);
    c.borderWidth = style.GetVar(StyleVarId::ButtonBorderWidth);

    switch (kind) {
    case ButtonKind::Primary:
        c.bg = accent;
        c.bgHovered = style.GetColor(StyleColorId::AccentHover);
        c.bgActive = style.GetColor(StyleColorId::AccentPressed);
        c.border = accent;
        c.borderHovered = accent;
        c.fg = style.GetColor(StyleColorId::OnAccent);
        c.borderWidth = 0.0f;
        break;
    case ButtonKind::Danger:
        c.bg = danger;
        c.bgHovered = Color::LerpC(danger, style.GetColor(StyleColorId::White), 0.14f);
        c.bgActive = Color::LerpC(danger, style.GetColor(StyleColorId::Text), 0.2f);
        c.border = danger;
        c.borderHovered = danger;
        c.fg = style.GetColor(StyleColorId::White);
        c.borderWidth = 0.0f;
        break;
    case ButtonKind::Warning:
        c.bg = warning;
        c.bgHovered = Color::LerpC(warning, style.GetColor(StyleColorId::White), 0.14f);
        c.bgActive = warning;
        c.border = warning;
        c.borderHovered = warning;
        c.fg = style.GetColor(StyleColorId::Text);
        c.borderWidth = 0.0f;
        break;
    case ButtonKind::Ghost:
        c.bg = Color(0, 0, 0, 0);
        c.bgHovered = cardHover;
        c.bgActive = card;
        c.border = Color(0, 0, 0, 0);
        c.borderHovered = Color(0, 0, 0, 0);
        c.fg = style.GetColor(StyleColorId::TextDim);
        c.borderWidth = 0.0f;
        break;
    case ButtonKind::Secondary:
    default:
        c.bg = field;
        c.bgHovered = fieldHover;
        c.bgActive = cardHover;
        c.border = border;
        c.borderHovered = Color::LerpC(border, style.GetColor(StyleColorId::White), 0.12f);
        c.fg = style.GetColor(StyleColorId::Text);
        break;
    }
    return c;
}

static bool ButtonImpl(const char* label, Icon icon, IconSlot slot, ButtonKind kind,
                       const Vec2& sizeIn, bool enabled, bool* hoveredOut) {
    Context& ctx = Context::Get();
    const u32 id = ctx.GetID(label != nullptr ? label : "##button");
    const ButtonColors c = ResolveButton(kind, ctx.GetStyle());

    const f32 fontSize = ctx.GetVar(StyleVarId::TextSize);
    Font* font = ctx.FontAt(fontSize);
    const bool hasLabel = label != nullptr && *label != '\0';
    const f32 labelW = hasLabel ? TextWidth(font, label) : 0.0f;
    const f32 iconSize = icon != Icon::None ? c.height * 0.56f : 0.0f;
    const f32 gap = (hasLabel && icon != Icon::None) ? c.height * 0.26f : 0.0f;

    Vec2 size = sizeIn;
    if (size.x <= 0.0f) {
        size.x = c.paddingX * 2.0f + labelW + iconSize + gap;
    }
    if (size.y <= 0.0f) size.y = c.height;

    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + size);

    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r = ctx.ItemBehavior(
        id, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);
    if (hoveredOut != nullptr) *hoveredOut = r.hovered;

    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const Color bg = ResolveInteractive(c.bg, c.bgHovered, c.bgActive, hover, press);
    const Color border = hover > 0.01f ? c.borderHovered : c.border;

    // Primary buttons carry a soft accent halo that strengthens on hover. One
    // extra primitive, no blur pass - the shader does the falloff.
    f32 glow = 0.0f;
    Color glowColor(0, 0, 0, 0);
    if (kind == ButtonKind::Primary) {
        glow = 2.0f + hover * 5.0f;
        glowColor = ctx.GetColor(StyleColorId::Glow);
    }

    ctx.AddBox(bounds, bg, CornerRadius(c.radius), c.borderWidth, border, glow, glowColor);

    const Color fg = enabled ? Color::LerpC(c.fg, ctx.GetColor(StyleColorId::White), hover * 0.25f)
                             : c.fgDisabled;

    if (icon != Icon::None) {
        const f32 total = iconSize + gap + labelW;
        f32 ix = bounds.GetCenter().x - total * 0.5f;
        if (slot == IconSlot::Right) ix += labelW + gap;

        const Rect iconRect(ix, bounds.GetCenter().y - iconSize * 0.5f, ix + iconSize,
                            bounds.GetCenter().y + iconSize * 0.5f);
        DrawIcon(ctx.MutableDrawList(), icon, iconRect, Max(1.2f, size.y * 0.055f), fg);

        if (hasLabel) {
            const f32 tx = (slot == IconSlot::Right) ? ix - gap - labelW : ix + iconSize + gap;
            DrawLabel(ctx, Rect(tx, bounds.min.y, tx + labelW, bounds.max.y), label, font, fg,
                      TextAlign::Left);
        }
    } else if (hasLabel) {
        DrawLabel(ctx, bounds, label, font, fg, TextAlign::Center);
    }

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(size);
    ctx.NewLine();
    return r.clicked && enabled;
}

bool Button(const char* label, ButtonKind kind, const Vec2& size, bool enabled) {
    return ButtonImpl(label, Icon::None, IconSlot::None, kind, size, enabled, nullptr);
}

bool ButtonWithIcon(const char* label, Icon icon, ButtonKind kind, IconSlot slot,
                    const Vec2& size, bool enabled) {
    return ButtonImpl(label, icon, slot, kind, size, enabled, nullptr);
}

bool IconButton(Icon icon, const char* tooltip, ButtonKind kind, f32 size, bool enabled,
                const Color* tint) {
    Context& ctx = Context::Get();
    const ButtonColors c = ResolveButton(kind, ctx.GetStyle());
    if (size <= 0.0f) size = c.height;

    const u32 id = ctx.GetID("##iconbutton");
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(size, size));

    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r = ctx.ItemBehavior(
        id, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));
    const Color bg = ResolveInteractive(c.bg, c.bgHovered, c.bgActive, hover, press);

    ctx.AddBox(bounds, bg, CornerRadius(c.radius));

    Color fg = c.fg;
    if (tint != nullptr) fg = *tint;
    fg = enabled ? Color::LerpC(fg, ctx.GetColor(StyleColorId::Text), hover) : c.fgDisabled;

    const f32 glyph = size * 0.54f;
    const Rect glyphRect(bounds.GetCenter() - Vec2(glyph * 0.5f),
                         bounds.GetCenter() + Vec2(glyph * 0.5f));
    DrawIcon(ctx.MutableDrawList(), icon, glyphRect, Max(1.2f, size * 0.06f), fg);

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(Vec2(size, size));
    ctx.NewLine();

    if (tooltip != nullptr && *tooltip != '\0' && r.hovered) {
        ShowTooltip(tooltip, Rect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y));
    }
    return r.clicked && enabled;
}

// Checkbox

bool Checkbox(const char* label, bool* value, bool enabled) {
    Context& ctx = Context::Get();
    const u32 id = ctx.GetID(label != nullptr ? label : "##checkbox");
    const f32 box = ctx.GetVar(StyleVarId::CheckboxSize);
    const f32 radius = ctx.GetVar(StyleVarId::CheckboxRadius);

    Font* font = ctx.FontAt(ctx.GetVar(StyleVarId::TextSize));
    const bool hasLabel = label != nullptr && *label != '\0';
    const f32 labelW = hasLabel ? TextWidth(font, label) : 0.0f;
    const f32 gap = hasLabel ? ctx.GetVar(StyleVarId::Gap) : 0.0f;

    const Rect bounds(ctx.CursorPos(),
                      ctx.CursorPos() + Vec2(box + gap + labelW, box));

    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r = ctx.ItemBehavior(
        id, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    const bool on = (value != nullptr && *value);
    // Snap the tick at 0.62 so it cannot be flickered by a fast double-click.
    const f32 anim = ctx.AnimFloat(id ^ 0x1a2b3c4du, on ? 1.0f : 0.0f,
                                   ctx.GetVar(StyleVarId::ToggleRate));

    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const Rect boxRect(ctx.CursorPos(), ctx.CursorPos() + Vec2(box, box));
    const Color accent = ctx.GetColor(StyleColorId::Accent);
    const Color bg = anim > 0.5f
        ? Color::LerpC(ctx.GetColor(StyleColorId::FieldBgHover), accent, (anim - 0.5f) * 2.0f)
        : Color::LerpC(ctx.GetColor(StyleColorId::FieldBg),
                       Color::LerpC(ctx.GetColor(StyleColorId::FieldBgHover), accent, 0.35f), hover);
    const Color border = Color::LerpC(ctx.GetColor(StyleColorId::FieldBorder),
                                      accent, anim * 0.9f);

    ctx.AddBox(boxRect, bg.WithAlpha(bg.a * (1.0f - press * 0.18f)), CornerRadius(radius), 1.0f,
                  border);

    // Tick, drawn only once it has room.
    if (anim > 0.02f) {
        const Color tick = ctx.GetColor(StyleColorId::OnAccent);
        const f32 s = box * 0.30f;
        const Vec2 c = boxRect.GetCenter();
        const f32 g = (1.0f - anim) * box * 0.35f;
        ctx.AddLine(Vec2(c.x - s - g, c.y + g * 0.4f), Vec2(c.x - s * 0.2f, c.y + s - g * 0.2f),
                    Max(1.6f, box * 0.11f), tick);
        ctx.AddLine(Vec2(c.x - s * 0.2f, c.y + s - g * 0.2f), Vec2(c.x + s - g, c.y - s),
                    Max(1.6f, box * 0.11f), tick);
    }

    if (hasLabel) {
        const Color fg = enabled ? ctx.GetColor(StyleColorId::Text)
                                 : ctx.GetColor(StyleColorId::TextDisabled);
        DrawLabel(ctx, Rect(boxRect.max.x + gap, bounds.min.y, bounds.max.x, bounds.max.y),
                  label, font, fg, TextAlign::Left);
    }

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (r.clicked && enabled && value != nullptr) *value = !*value;
    return r.clicked && enabled;
}

// Toggle switch (the pill in the reference UI)

bool ToggleSwitch(bool* value, bool enabled, f32 widthIn, f32 heightIn) {
    Context& ctx = Context::Get();
    const u32 id = ctx.GetID("##toggle");
    const f32 w = widthIn > 0.0f ? widthIn : ctx.GetVar(StyleVarId::ToggleWidth);
    const f32 h = heightIn > 0.0f ? heightIn : ctx.GetVar(StyleVarId::ToggleHeight);
    const f32 radius = h * 0.5f;
    const f32 knobR = h * 0.5f - ctx.GetVar(StyleVarId::ToggleKnobRadius) * 0.0f - 2.0f;

    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));

    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r = ctx.ItemBehavior(
        id, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    const bool on = (value != nullptr && *value);
    const f32 anim = ctx.AnimFloat(id ^ 0x77aa11u, on ? 1.0f : 0.0f,
                                   ctx.GetVar(StyleVarId::ToggleRate));
    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const Color trackOff = Color::LerpC(ctx.GetColor(StyleColorId::Track),
                                         ctx.GetColor(StyleColorId::TrackHover), hover);
    const Color track = Color::LerpC(trackOff, ctx.GetColor(StyleColorId::Accent), anim);

    // Pressing nudges the whole track; it reads as physical without moving the
    // knob, which stays on its animated position.
    const f32 pressShift = press * 1.2f;
    const Rect trackRect(bounds.min.x + pressShift, bounds.min.y, bounds.max.x + pressShift,
                         bounds.max.y);
    ctx.AddBox(trackRect, track, CornerRadius(radius));

    const f32 pad = 2.5f;
    const f32 travel = bounds.Width() - knobR * 2.0f - pad * 2.0f;
    const f32 kx = bounds.min.x + pad + knobR + travel * anim;
    const Vec2 kc(kx, bounds.GetCenter().y);
    const Rect knobRect(kc.x - knobR, kc.y - knobR, kc.x + knobR, kc.y + knobR);

    const Color knob = enabled
        ? Color::LerpC(ctx.GetColor(StyleColorId::Thumb), ctx.GetColor(StyleColorId::White), 0.0f)
        : ctx.GetColor(StyleColorId::TextDisabled);
    ctx.AddBox(knobRect, knob, CornerRadius(knobR));

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (r.clicked && enabled && value != nullptr) *value = !*value;
    return r.clicked && enabled;
}

// Slider

namespace {
/// Shared implementation. `vertical` picks the axis.
bool SliderImpl(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt,
                bool vertical, f32 fixedHeight, bool enabled) {
    Context& ctx = Context::Get();
    const u32 id = ctx.GetID(label != nullptr ? label : "##slider");
    const u32 idAnim = id ^ 0x2f81a3u;

    Font* font = ctx.FontAt(ctx.GetVar(StyleVarId::TextSizeSmall));
    const bool hasLabel = label != nullptr && *label != '\0';

    char valueBuf[64];
    if (fmt != nullptr && fmt[0] != '\0') {
        std::snprintf(valueBuf, sizeof(valueBuf), fmt, value != nullptr ? *value : 0.0f);
    } else {
        valueBuf[0] = '\0';
    }
    const f32 valueW = valueBuf[0] != '\0' ? TextWidth(font, valueBuf) : 0.0f;
    const f32 labelW = hasLabel ? TextWidth(ctx.FontAt(ctx.GetVar(StyleVarId::TextSize)), label) : 0.0f;

    const f32 trackW = vertical ? 0.0f : 8.0f;
    const f32 totalW = vertical
        ? ctx.GetVar(StyleVarId::SliderHeight)
        : (labelW > 0.0f ? labelW + ctx.GetVar(StyleVarId::GapLargeX) : 0.0f) + trackW +
              (valueW > 0.0f ? valueW + ctx.GetVar(StyleVarId::Gap) : 0.0f);
    const f32 totalH = vertical ? fixedHeight : ctx.GetVar(StyleVarId::SliderHeight);

    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(totalW, totalH));

    // The interactive track is the full widget height but inset so it matches
    // the label's optical height.
    Rect trackRect = bounds;
    if (!vertical) {
        trackRect.min.x = labelW > 0.0f ? bounds.min.x + labelW + ctx.GetVar(StyleVarId::GapLargeX)
                                        : bounds.min.x;
        trackRect.max.x = valueW > 0.0f ? bounds.max.x - valueW - ctx.GetVar(StyleVarId::Gap)
                                        : bounds.max.x;
        trackRect.min.y = bounds.min.y + (bounds.Height() - ctx.GetVar(StyleVarId::SliderHeight)) * 0.5f;
        trackRect.max.y = trackRect.min.y + ctx.GetVar(StyleVarId::SliderHeight);
    } else {
        trackRect.max.x = bounds.min.x + ctx.GetVar(StyleVarId::SliderHeight);
        trackRect.min.y = bounds.min.y;
        trackRect.max.y = bounds.max.y;
    }

    const f32 v = (value != nullptr) ? *value : vmin;
    f32 t = (vmax > vmin) ? (v - vmin) / (vmax - vmin) : 0.0f;
    t = Clamp01(t);

    // Smooth the displayed position so a keyboard step is not a jump.
    const f32 tSmooth = ctx.AnimFloat(idAnim, t, ctx.GetVar(StyleVarId::ScrollRate));

    const Rect hitRect = vertical
        ? Rect(bounds.min.x, bounds.min.y, bounds.min.x + ctx.GetVar(StyleVarId::SliderHeight) * 2.0f,
               bounds.max.y)
        : Rect(trackRect.min.x - ctx.GetVar(StyleVarId::SliderThumbRadius),
               bounds.min.y - 2.0f,
               trackRect.max.x + ctx.GetVar(StyleVarId::SliderThumbRadius), bounds.max.y + 2.0f);

    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r =
        ctx.ItemBehavior(id, hitRect, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    bool changed = false;
    if (enabled && (r.held || r.pressed) && value != nullptr) {
        f32 nt = t;
        if (vertical) {
            nt = 1.0f - (ctx.Input().mousePos.y - trackRect.min.y) / Max(1.0f, trackRect.Height());
        } else {
            nt = (ctx.Input().mousePos.x - trackRect.min.x) / Max(1.0f, trackRect.Width());
        }
        nt = Clamp01(nt);
        const f32 nv = vmin + nt * (vmax - vmin);
        if (!NearlyEqual(nv, *value, 0.0001f)) {
            *value = nv;
            changed = true;
        }
    }

    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const f32 railT = ctx.GetVar(StyleVarId::SliderRailThickness);
    const f32 thumbR = ctx.GetVar(StyleVarId::SliderThumbRadius) + press * 0.8f;

    // Rail (unfilled) and fill.
    if (vertical) {
        const f32 cx = trackRect.GetCenter().x;
        const f32 x0 = cx - railT * 0.5f, x1 = cx + railT * 0.5f;
        ctx.AddBox(Rect(x0, trackRect.min.y, x1, trackRect.max.y),
                   ctx.GetColor(StyleColorId::SliderRail), CornerRadius(railT * 0.5f));
        const f32 fillTop = trackRect.max.y - trackRect.Height() * tSmooth;
        ctx.AddBox(Rect(x0, fillTop, x1, trackRect.max.y),
                   Color::LerpC(ctx.GetColor(StyleColorId::Accent), ctx.GetColor(StyleColorId::AccentHover),
                                hover),
                   CornerRadius(railT * 0.5f));
        const f32 ty = fillTop;
        const Rect thumb(cx - thumbR, ty - thumbR, cx + thumbR, ty + thumbR);
        ctx.AddBox(thumb, ctx.GetColor(StyleColorId::Thumb), CornerRadius(thumbR));
    } else {
        const f32 cy = trackRect.GetCenter().y;
        const f32 y0 = cy - railT * 0.5f, y1 = cy + railT * 0.5f;
        ctx.AddBox(Rect(trackRect.min.x, y0, trackRect.max.x, y1),
                   ctx.GetColor(StyleColorId::SliderRail), CornerRadius(railT * 0.5f));
        const f32 fillX = trackRect.min.x + trackRect.Width() * tSmooth;
        ctx.AddBox(Rect(trackRect.min.x, y0, Max(fillX, trackRect.min.x + railT), y1),
                   Color::LerpC(ctx.GetColor(StyleColorId::Accent), ctx.GetColor(StyleColorId::AccentHover),
                                hover),
                   CornerRadius(railT * 0.5f));
        const Rect thumb(fillX - thumbR, cy - thumbR, fillX + thumbR, cy + thumbR);
        // The thumb gets an accent halo while dragging, so it reads as grabbable.
        const f32 glow = press * 4.0f;
        ctx.AddBox(thumb, ctx.GetColor(StyleColorId::Thumb), CornerRadius(thumbR), 0.0f,
                   ctx.GetColor(StyleColorId::Thumb), glow, ctx.GetColor(StyleColorId::Glow));
    }

    if (hasLabel) {
        Font* labelFont = ctx.FontAt(ctx.GetVar(StyleVarId::TextSize));
        const Color fg = enabled ? ctx.GetColor(StyleColorId::Text)
                                 : ctx.GetColor(StyleColorId::TextDisabled);
        DrawLabel(ctx, Rect(bounds.min.x, bounds.min.y, bounds.min.x + labelW, bounds.max.y),
                  label, labelFont, fg, TextAlign::Left);
    }
    if (valueBuf[0] != '\0') {
        const Color fg = enabled ? ctx.GetColor(StyleColorId::TextDim)
                                 : ctx.GetColor(StyleColorId::TextDisabled);
        DrawLabel(ctx, Rect(bounds.max.x - valueW, bounds.min.y, bounds.max.x, bounds.max.y),
                  valueBuf, font, fg, TextAlign::Right);
    }

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();
    return changed;
}

} // namespace

bool SliderFloat(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt, bool enabled) {
    return SliderImpl(label, value, vmin, vmax, fmt, false, 0.0f, enabled);
}

bool SliderFloatVertical(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt,
                         f32 height, bool enabled) {
    return SliderImpl(label, value, vmin, vmax, fmt, true, height, enabled);
}

bool SliderInt(const char* label, int* value, int vmin, int vmax, const char* suffix,
               bool enabled) {
    char fmt[48];
    std::snprintf(fmt, sizeof(fmt), "%%d%s", suffix != nullptr ? suffix : "");
    f32 asFloat = static_cast<f32>(value != nullptr ? *value : 0);
    const bool changed = SliderImpl(label, &asFloat, static_cast<f32>(vmin),
                                    static_cast<f32>(vmax), fmt, false, 0.0f, enabled);
    if (value != nullptr) *value = static_cast<int>(asFloat);
    return changed;
}

} // namespace argon::ui
