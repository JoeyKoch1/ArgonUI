// ArgonUI - src/Widgets/Pickers.cpp
// Combo / segmented control / text fields / search / tooltip queue.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "argonui/Widgets/Widgets.h"

namespace argon::ui {

namespace {

/// Clamps a byte offset into the string onto a UTF-8 boundary.
size_t CaretClamp(const std::string& s, u32 offset) {
    size_t i = std::min<size_t>(offset, s.size());
    while (i > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u) {
        ++i;
    }
    return i;
}

enum class TextAlign2 { Left, Center, Right };

f32 DrawText(Context& ctx, const Rect& bounds, const char* text, Font* font, const Color& color,
             TextAlign2 align) {
    if (font == nullptr || text == nullptr || *text == '\0') return 0.0f;
    const size_t len = std::strlen(text);
    const size_t shown = font->Ellipsize(text, len, bounds.Width());
    if (shown == 0) return 0.0f;
    const f32 w = font->MeasureLine(text, shown);
    f32 x = bounds.min.x;
    if (align == TextAlign2::Center) x = bounds.GetCenter().x - w * 0.5f;
    else if (align == TextAlign2::Right) x = bounds.max.x - w;
    const f32 y = bounds.min.y + (bounds.Height() - font->LineHeight()) * 0.5f + font->Ascent();
    ctx.AddText(Vec2(x, y), text, color, font);
    return w;
}

/// Positions a popup below `anchor`, flipping up or clamping to the display.
Rect PlacePopup(const Rect& anchor, f32 w, f32 h, Vec2 display, f32 gap = 4.0f) {
    f32 x = anchor.min.x;
    f32 y = anchor.max.y + gap;
    if (y + h > display.y) {
        const f32 above = anchor.min.y - gap - h;
        y = above >= 0.0f ? above : Max(0.0f, display.y - h);
    }
    x = Clamp(x, 0.0f, Max(0.0f, display.x - w));
    y = Clamp(y, 0.0f, Max(0.0f, display.y - h));
    return Rect(x, y, x + w, y + h);
}

} // namespace

// ===========================================================================
// Segmented control
// ===========================================================================

int SegmentedControl(const char* id, const char* const* options, int count, int* selected,
                     f32 itemWidth) {
    Context& ctx = Context::Get();
    const u32 baseId = ctx.GetID(id);

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall));
    const f32 h = ctx.Var(StyleVarId::ButtonHeight) - 2.0f;
    const f32 padX = 10.0f;

    f32 w = itemWidth;
    if (w <= 0.0f) {
        f32 widest = 0.0f;
        for (int i = 0; i < count; ++i) {
            widest = Max(widest, font != nullptr ? font->MeasureLine(options[i], std::strlen(options[i])) : 0.0f);
        }
        w = widest + padX * 2.0f;
    }
    const f32 trackW = w * count + 6.0f;
    const Rect trackRect(ctx.CursorPos(), ctx.CursorPos() + Vec2(trackW, h + 6.0f));
    const f32 trackRadius = (h + 6.0f) * 0.5f;

    ctx.AddBox(trackRect, ctx.Color(StyleColorId::FieldBg), CornerRadius(trackRadius),
               1.0f, ctx.Color(StyleColorId::FieldBorder));

    // Animated pill position. Uses AnimVec2 so the slide is frame-rate safe.
    int cur = (selected != nullptr) ? *selected : 0;
    cur = Clamp(cur, 0, Max(0, count - 1));
    const Vec2 target(w * static_cast<f32>(cur), 0.0f);
    const Vec2 pos = ctx.AnimVec2(baseId ^ 0x5e11u, target, ctx.Var(StyleVarId::HoverRate) * 1.4f);

    const Rect pillRect(trackRect.min.x + 3.0f + pos.x, trackRect.min.y + 3.0f,
                        trackRect.min.x + 3.0f + pos.x + w, trackRect.max.y - 3.0f);
    ctx.AddBox(pillRect, ctx.Color(StyleColorId::CardBgHover), CornerRadius(trackRadius),
               1.0f, ctx.Color(StyleColorId::CardBorder));

    const ItemResult trackHit = ctx.ItemBehavior(baseId ^ 0x77u, trackRect,
                                                 ItemFlags::AllowOverlap);

    int result = cur;
    for (int i = 0; i < count; ++i) {
        const u32 itemId = ctx.GetID(options[i]);
        const Rect itemRect(trackRect.min.x + 3.0f + w * static_cast<f32>(i), trackRect.min.y + 3.0f,
                            trackRect.min.x + 3.0f + w * static_cast<f32>(i + 1),
                            trackRect.max.y - 3.0f);
        const ItemResult r = ctx.ItemBehavior(itemId, itemRect, ItemFlags::AllowOverlap);
        const Color fg = (i == cur) ? ctx.Color(StyleColorId::Text)
                                    : ctx.Color(StyleColorId::TextDim);
        DrawText(ctx, itemRect, options[i], font, fg, TextAlign2::Center);
        if (r.clicked) result = i;
    }
    (void)trackHit;

    ctx.ItemSize(trackRect.Size());
    ctx.NewLine();
    if (selected != nullptr && result != *selected) {
        *selected = result;
        return 1;
    }
    return 0;
}

// ===========================================================================
// Combo
// ===========================================================================

int PopupList(Context& ctx, const char* id, const Rect& anchor, const char* const* labels,
              int count, int* selected, f32 width, bool* open, const Icon* icons) {
    const u32 baseId = ctx.GetID(id);
    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSize));
    const f32 itemH = ctx.Var(StyleVarId::PopupItemHeight);
    const f32 padX = ctx.Var(StyleVarId::PopupItemPaddingX);
    const f32 padY = ctx.Var(StyleVarId::PopupPadding);

    f32 w = width;
    if (w <= 0.0f) {
        w = 120.0f;
        for (int i = 0; i < count; ++i) {
            w = Max(w, font->MeasureLine(labels[i], std::strlen(labels[i])) + padX * 2.0f + 28.0f);
        }
    }
    const f32 contentH = itemH * static_cast<f32>(count);
    const Rect rect = PlacePopup(anchor, w, contentH + padY * 2.0f, ctx.Frame().displaySize);

    bool isOpen = open != nullptr && *open;
    if (ctx.PopupDismissRequested()) isOpen = false;

    // Opening/closing is animated; below ~0.05 the popup is fully closed.
    const f32 anim = ctx.AnimTo(baseId ^ 0xa1b2c3u, isOpen ? 1.0f : 0.0f, 0.0f);
    if (anim < 0.05f) {
        if (open != nullptr) *open = false;
        return -1;
    }

    const u32 z = ctx.PushZ();
    ctx.SetPopupCapture(rect);

    // Slide down + fade in from the top edge.
    const f32 slide = (1.0f - anim) * -6.0f;
    const Rect visible(rect.min.x, rect.min.y + slide, rect.max.x, rect.max.y + slide);

    ctx.AddBox(visible, ctx.Color(StyleColorId::PanelBg).WithAlpha(ctx.Alpha()),
               CornerRadius(ctx.Var(StyleVarId::PopupRadius)),
               1.0f, ctx.Color(StyleColorId::CardBorder),
               ctx.Var(StyleVarId::PopupShadowSize),
               ctx.Color(StyleColorId::Shadow));

    ctx.PushClipRect(visible);

    int result = -1;
    for (int i = 0; i < count; ++i) {
        const u32 itemId = ctx.GetID(labels[i]);
        const Rect itemRect(rect.min.x + padX, rect.min.y + padY + itemH * static_cast<f32>(i),
                            rect.max.x - padX, rect.min.y + padY + itemH * static_cast<f32>(i + 1));
        const ItemResult r = ctx.ItemBehavior(itemId, itemRect);

        if (r.hovered || (selected != nullptr && *selected == i)) {
            ctx.AddBox(itemRect,
                       (selected != nullptr && *selected == i)
                           ? ctx.Color(StyleColorId::Selection)
                           : ctx.Color(StyleColorId::CardBgHover),
                       CornerRadius(ctx.Var(StyleVarId::ListItemRadius)));
        }

        f32 tx = itemRect.min.x + padX;
        if (icons != nullptr && icons[i] != Icon::None) {
            const f32 gs = itemH * 0.52f;
            const Rect ir(itemRect.min.x + padX, itemRect.GetCenter().y - gs * 0.5f,
                          itemRect.min.x + padX + gs, itemRect.GetCenter().y + gs * 0.5f);
            DrawIcon(ctx.MutableDrawList(), icons[i], ir, 1.6f,
                     ctx.Color(StyleColorId::TextDim));
            tx = ir.max.x + 10.0f;
        }
        DrawText(ctx, Rect(tx, itemRect.min.y, itemRect.max.x - padX, itemRect.max.y),
                 labels[i], font, ctx.Color(StyleColorId::Text), TextAlign2::Left);

        if (r.clicked) {
            result = i;
            if (open != nullptr) *open = false;
        }
    }

    ctx.PopClipRect();
    ctx.PopZ();
    (void)z;
    return result;
}

int Combo(const char* id, const char* const* options, int count, int* selected, f32 width,
          const char* placeholder) {
    Context& ctx = Context::Get();
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSize));
    const f32 h = ctx.Var(StyleVarId::FrameHeight);
    const f32 radius = ctx.Var(StyleVarId::FrameRadius);
    const f32 padX = ctx.Var(StyleVarId::FramePaddingX);

    f32 w = width;
    if (w <= 0.0f) {
        w = 120.0f;
        for (int i = 0; i < count; ++i) {
            w = Max(w, font->MeasureLine(options[i], std::strlen(options[i])) + padX * 2.0f + 34.0f);
        }
    }
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));

    bool open = st.opened;
    if (ctx.PopupDismissRequested() && open) {
        open = false;
        st.opened = false;
    }

    const ItemResult r = ctx.ItemBehavior(baseId, bounds);
    if (r.clicked) {
        open = !open;
        st.opened = open;
    }

    const bool focused = open || ctx.ActiveId() == baseId;
    const Color bg = focused ? ctx.Color(StyleColorId::FieldBgHover)
                             : ctx.Color(StyleColorId::FieldBg);
    const Color border = focused ? ctx.Color(StyleColorId::FieldBorderFocus)
                                 : ctx.Color(StyleColorId::FieldBorder);
    ctx.AddBox(bounds, bg, CornerRadius(radius), 1.0f, border);

    const int cur = (selected != nullptr) ? *selected : -1;
    const bool hasValue = cur >= 0 && cur < count;
    const char* text = hasValue ? options[cur] : (placeholder != nullptr ? placeholder : "");
    const Color fg = hasValue ? ctx.Color(StyleColorId::Text) : ctx.Color(StyleColorId::TextMuted);
    DrawText(ctx, Rect(bounds.min.x + padX, bounds.min.y, bounds.max.x - 22.0f, bounds.max.y),
             text, font, fg, TextAlign2::Left);

    // Chevron, rotated 180 degrees while open.
    const f32 cs = h * 0.42f;
    const Rect chev(bounds.max.x - padX - cs, bounds.GetCenter().y - cs * 0.5f,
                    bounds.max.x - padX, bounds.GetCenter().y + cs * 0.5f);
    const f32 rot = ctx.AnimFloat(baseId ^ 0x3c3c3cu, open ? 1.0f : 0.0f, ctx.Var(StyleVarId::HoverRate));
    if (rot < 0.5f) {
        DrawIcon(ctx.MutableDrawList(), Icon::ChevronDown, chev, 1.6f,
                 ctx.Color(StyleColorId::TextDim));
    } else {
        DrawIcon(ctx.MutableDrawList(), Icon::ChevronUp, chev, 1.6f,
                 ctx.Color(StyleColorId::TextDim));
    }

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (!open) return -1;
    const int pick = PopupList(ctx, id, bounds, options, count, selected, w, &st.opened);
    if (pick >= 0 && selected != nullptr) *selected = pick;
    return pick;
}

// ===========================================================================
// Text fields
// ===========================================================================

namespace {

/// Shared caret/selection-free single-line editor.
/// Returns true when the text changed.
bool TextFieldImpl(Context& ctx, const char* id, std::string& value, const char* placeholder,
                   bool enabled, Icon icon, const char* label) {
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSize));
    const f32 h = ctx.Var(StyleVarId::FrameHeight);
    const f32 radius = ctx.Var(StyleVarId::FrameRadius);
    const f32 padX = ctx.Var(StyleVarId::FramePaddingX);

    const f32 availW = ctx.ContentRegionAvail().Width();
    f32 w = availW;
    if (w < h) w = h;

    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));

    const bool focused = ctx.FocusId() == baseId;
    if (!enabled) ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);

    const ItemResult r =
        ctx.ItemBehavior(baseId, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    bool changed = false;

    if (enabled && r.pressed) {
        ctx.SetFocusId(baseId);
        ctx.SetActiveId(baseId);
    }
    if (ctx.FocusId() == baseId && ctx.Input().IsMouseClicked(Mouse_Left) &&
        !bounds.Contains(ctx.Input().mousePos)) {
        ctx.ClearFocusId();
    }
    if (ctx.Input().IsKeyPressed(Key_Escape) && ctx.FocusId() == baseId) {
        ctx.ClearFocusId();
    }
    if (ctx.Input().IsKeyPressed(Key_Tab)) {
        if (ctx.FocusId() == baseId) ctx.ClearFocusId();
    }

    const f32 innerX = bounds.min.x + padX + (icon != Icon::None ? h * 0.62f : 0.0f);
    const Rect textRect(innerX, bounds.min.y, bounds.max.x - padX, bounds.max.y);

    if (enabled && ctx.FocusId() == baseId) {
        const InputState& in = ctx.Input();

        // Mouse caret placement.
        if (r.pressed) {
            const f32 local = Clamp(ctx.Input().mousePos.x - innerX, 0.0f,
                                    Max(0.0f, textRect.Width()));
            size_t best = 0;
            f32 bestD = 1e9f;
            const size_t n = value.size();
            for (size_t i = 0; i <= n; ++i) {
                const f32 wpx = font->MeasureLine(value.c_str(), i);
                const f32 d = std::fabs(wpx - local);
                if (d < bestD) { bestD = d; best = i; }
            }
            st.payload = static_cast<u32>(best);
        }

        for (u32 cp : in.inputCodepoints) {
            if (cp >= 32 && cp != 127) {
                char buf[4];
                const size_t n = EncodeUtf8(cp, buf);
                const size_t caret = CaretClamp(value, st.payload);
                value.insert(caret, buf, n);
                st.payload = static_cast<u32>(caret + n);
                changed = true;
            }
        }
        if (in.IsKeyPressed(Key_Backspace) && !value.empty()) {
            const size_t caret = CaretClamp(value, st.payload);
            if (caret > 0) {
                size_t prev = caret - 1;
                while (prev > 0 && (static_cast<unsigned char>(value[prev]) & 0xC0u) == 0x80u) {
                    --prev;
                }
                value.erase(prev, caret - prev);
                st.payload = static_cast<u32>(prev);
                changed = true;
            }
        }
        if (in.IsKeyPressed(Key_Delete)) {
            const size_t caret = CaretClamp(value, st.payload);
            if (caret < value.size()) {
                size_t next = caret + 1;
                while (next < value.size() &&
                       (static_cast<unsigned char>(value[next]) & 0xC0u) == 0x80u) {
                    ++next;
                }
                value.erase(caret, next - caret);
                changed = true;
            }
        }
        if (in.IsKeyPressed(Key_LeftArrow) && st.payload > 0) {
            --st.payload;
        }
        if (in.IsKeyPressed(Key_RightArrow) && st.payload < value.size()) {
            ++st.payload;
        }
        if (in.IsKeyPressed(Key_Home)) st.payload = 0;
        if (in.IsKeyPressed(Key_End)) st.payload = static_cast<u32>(value.size());
    }

    const f32 fgAlpha = enabled ? 1.0f : 0.6f;
    if (!value.empty()) {
        DrawText(ctx, textRect, value.c_str(), font,
                 ctx.Color(StyleColorId::Text).WithAlpha(fgAlpha), TextAlign2::Left);
    } else if (placeholder != nullptr && placeholder[0] != '\0') {
        DrawText(ctx, textRect, placeholder, font,
                 ctx.Color(StyleColorId::TextMuted).WithAlpha(fgAlpha), TextAlign2::Left);
    }

    if (icon != Icon::None) {
        const f32 gs = h * 0.52f;
        const Rect ir(bounds.min.x + padX, bounds.GetCenter().y - gs * 0.5f,
                      bounds.min.x + padX + gs, bounds.GetCenter().y + gs * 0.5f);
        DrawIcon(ctx.MutableDrawList(), icon, ir, 1.6f, ctx.Color(StyleColorId::TextDim));
    }

    // Blinking caret while focused.
    if (ctx.FocusId() == baseId && enabled) {
        const f32 blink = 1.0f + std::sin(ctx.Time() * 6.0f) * 0.5f;
        const size_t caret = CaretClamp(value, st.payload);
        const f32 cx = textRect.min.x + font->MeasureLine(value.c_str(), caret);
        const Rect caretRect(cx, textRect.min.y + 4.0f, cx + 1.6f, textRect.max.y - 4.0f);
        ctx.AddBox(caretRect, ctx.Color(StyleColorId::Accent).WithAlpha(0.35f + blink * 0.65f));
    }

    const Color bg = ctx.FocusId() == baseId ? ctx.Color(StyleColorId::FieldBgHover)
                                             : ctx.Color(StyleColorId::FieldBg);
    const Color border = ctx.FocusId() == baseId ? ctx.Color(StyleColorId::Accent)
                                                 : ctx.Color(StyleColorId::FieldBorder);
    ctx.AddBox(bounds, bg, CornerRadius(radius), 1.0f, border);

    if (!enabled) ctx.PopStyleVar(1);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();
    return changed;
}

} // namespace

bool TextField(Context& ctx, const char* id, char* buffer, size_t bufferSize,
               const char* placeholder, bool enabled, Icon icon) {
    std::string v(buffer != nullptr ? std::string(buffer) : std::string());
    const bool changed = TextFieldImpl(ctx, id, v, placeholder, enabled, icon, nullptr);
    if (changed && buffer != nullptr && bufferSize > 0) {
        std::snprintf(buffer, bufferSize, "%s", v.c_str());
    }
    return changed;
}

bool TextField(Context& ctx, const char* id, std::string& value, const char* placeholder,
               bool enabled, Icon icon) {
    return TextFieldImpl(ctx, id, value, placeholder, enabled, icon, nullptr);
}

bool SearchField(Context& ctx, const char* id, std::string& query, const char* placeholder,
                 bool enabled) {
    return TextFieldImpl(ctx, id, query, placeholder != nullptr ? placeholder : "Search",
                         enabled, Icon::Search, nullptr);
}

bool DragFloat(Context& ctx, const char* id, f32* value, f32 speed, f32 vmin, f32 vmax,
               const char* fmt) {
    if (value == nullptr) return false;
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall));
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt != nullptr ? fmt : "%.2f", *value);

    const f32 h = ctx.Var(StyleVarId::FrameHeight);
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(ctx.ContentRegionAvail().Width(), h));

    const ItemResult r = ctx.ItemBehavior(baseId, bounds);
    if (r.pressed) {
        ctx.SetActiveId(baseId);
        st.value2 = ctx.Input().mousePos.x;
    }
    bool changed = false;
    if (r.held && r.dragging) {
        const f32 dx = ctx.Input().mousePos.x - st.value2;
        st.value2 = ctx.Input().mousePos.x;
        f32 nv = *value + dx * speed;
        if (vmax > vmin) nv = Clamp(nv, vmin, vmax);
        if (!NearlyEqual(nv, *value)) { *value = nv; changed = true; }
    }
    if (ctx.ActiveId() == baseId && !ctx.Input().IsMouseDown(Mouse_Left)) ctx.ClearActiveId();

    const bool focused = ctx.ActiveId() == baseId;
    ctx.AddBox(bounds,
               focused ? ctx.Color(StyleColorId::FieldBgHover) : ctx.Color(StyleColorId::FieldBg),
               CornerRadius(ctx.Var(StyleVarId::FrameRadius)), 1.0f,
               focused ? ctx.Color(StyleColorId::Accent) : ctx.Color(StyleColorId::FieldBorder));
    DrawText(ctx, bounds, buf, font, ctx.Color(StyleColorId::Text), TextAlign2::Left);

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();
    return changed;
}

bool DragInt(Context& ctx, const char* id, int* value, f32 speed, int vmin, int vmax) {
    if (value == nullptr) return false;
    f32 asFloat = static_cast<f32>(*value);
    const bool changed = DragFloat(ctx, id, &asFloat, speed, static_cast<f32>(vmin),
                                   static_cast<f32>(vmax), "%d");
    *value = static_cast<int>(asFloat);
    return changed;
}

void TextBlock(Context& ctx, const char* text, const Color& color, f32 fontSize) {
    if (text == nullptr || *text == '\0') return;
    Font* font = ctx.FontAt(fontSize > 0.0f ? fontSize : ctx.Var(StyleVarId::TextSizeSmall));
    const f32 width = ctx.ContentRegionAvail().Width();
    std::vector<std::pair<size_t, size_t>> lines;
    font->WrapText(text, std::strlen(text), width, lines);

    const Color c = color.a > 0.0f ? color : ctx.Color(StyleColorId::TextDim);
    f32 y = ctx.CursorPos().y;
    for (const auto& line : lines) {
        ctx.AddText(Vec2(ctx.CursorPos().x, y + font->Ascent()), text + line.first, c, font);
        y += font->LineHeight();
    }
    ctx.ItemSize(Vec2(width, y - ctx.CursorPos().y));
    ctx.NewLine();
}

// ===========================================================================
// Tooltip queue
// ===========================================================================

namespace {
struct TooltipRequest {
    std::string text;
    Rect anchor;
};
std::vector<TooltipRequest> g_tooltips;
} // namespace

void ShowTooltip(const char* text, const Vec2& anchor) {
    ShowTooltip(text, Rect(anchor.x, anchor.y, anchor.x, anchor.y));
}

void ShowTooltip(const char* text, const Rect& anchor) {
    if (text == nullptr || *text == '\0') return;
    g_tooltips.push_back({std::string(text), anchor});
}

void ClearTooltips() { g_tooltips.clear(); }

void RenderTooltips(Context& ctx) {
    if (g_tooltips.empty()) return;

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall));
    const f32 padX = 9.0f, padY = 6.0f;

    // The last request wins - it is always the topmost hovered widget.
    const TooltipRequest& req = g_tooltips.back();
    const Vec2 size = font->MeasureText(req.text.c_str(), req.text.size());

    f32 x = req.anchor.GetCenter().x - size.x * 0.5f;
    f32 y = req.anchor.max.y + 8.0f;
    x = Clamp(x, 4.0f, Max(4.0f, ctx.Frame().displaySize.x - size.x - 8.0f));
    y = Clamp(y, 4.0f, Max(4.0f, ctx.Frame().displaySize.y - size.y - 8.0f));

    const Rect box(x, y, x + size.x + padX * 2.0f, y + size.y + padY * 2.0f);
    ctx.AddBox(box, Color::LerpC(ctx.Color(StyleColorId::Text), Color::Hex(0x0A0B0Du), 0.86f),
               CornerRadius(6.0f), 1.0f, ctx.Color(StyleColorId::Text).WithAlpha(0.14f),
               10.0f, ctx.Color(StyleColorId::Shadow));
    ctx.AddText(Vec2(box.min.x + padX, box.min.y + padY + font->Ascent()), req.text.c_str(),
                ctx.Color(StyleColorId::Text), font);
}

} // namespace argon::ui