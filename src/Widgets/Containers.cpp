// ArgonUI - src/Widgets/Containers.cpp
// Window frames, collapsible sections, tabs, sidebars, scroll areas, rows.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "argonui/Widgets/Widgets.h"

namespace argon::ui {

namespace {

enum class Align { Left, Center, Right };

f32 DrawTextC(Context& ctx, const Rect& bounds, const char* text, Font* font, const Color& color,
              Align align) {
    if (font == nullptr || text == nullptr || *text == '\0') return 0.0f;
    const size_t len = std::strlen(text);
    const size_t shown = font->Ellipsize(text, len, bounds.Width());
    if (shown == 0) return 0.0f;
    const f32 w = font->MeasureLine(text, shown);
    f32 x = bounds.min.x;
    if (align == Align::Center) x = bounds.GetCenter().x - w * 0.5f;
    else if (align == Align::Right) x = bounds.max.x - w;
    const f32 y = bounds.min.y + (bounds.Height() - font->LineHeight()) * 0.5f + font->Ascent();
    ctx.AddText(Vec2(x, y), text, color, font);
    return w;
}

/// Per-frame stacks so the Begin/End helpers can find their own geometry.
struct ContainerStack {
    // Section
    std::vector<u32> sectionId;
    std::vector<Rect> sectionTop;
    std::vector<Rect> sectionClip;
    std::vector<u32> sectionCheckpoint;
    std::vector<bool> sectionOpen;
    std::vector<f32> sectionY0;

    // Scroll area
    std::vector<Rect> scrollContent;

    // Modal
    std::vector<Rect> modalHeader;
    std::vector<Rect> modalBody;
    std::vector<Rect> modalFooter;

    // Window
    std::vector<Rect> windowRect;
};

ContainerStack& CS() {
    static ContainerStack s;
    return s;
}

} // namespace

// ===========================================================================
// Scroll area
// ===========================================================================

bool BeginScrollArea(Context& ctx, const char* id, f32 height, bool* scrollYOut,
                     bool showScrollbar) {
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    f32 h = height;
    if (h <= 0.0f) h = ctx.ContentRegionAvail().Height();
    const f32 w = ctx.ContentRegionAvail().Width();
    const Rect outer(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));

    const bool hovered = ctx.IsMouseHoveringRect(outer);
    bool scrolled = false;

    if (hovered && std::fabs(ctx.Input().mouseWheel) > 0.01f) {
        st.value -= ctx.Input().mouseWheel * 52.0f;
        st.value2 = -ctx.Input().mouseWheel * 9.0f;  // fling
        st.opened = true;
        scrolled = true;
    }
    if (st.opened) {
        st.value += st.value2 * ctx.DeltaTime() * 60.0f;
        st.value2 = Damp(st.value2, 0.0f, ctx.DeltaTime(), 11.0f);
        if (std::fabs(st.value2) < 0.05f) st.opened = false;
        scrolled = true;
    }
    st.value = Clamp(st.value, 0.0f, Max(0.0f, h - 1.0f));

    const Rect clip = IntersectStrict(outer, ctx.ClipRect());
    ctx.PushClipRect(clip);

    const Rect content(clip.min.x, clip.min.y - st.value, clip.max.x, clip.max.y);
    CS().scrollContent.push_back(content);
    ctx.BeginLayout(content);

    if (scrollYOut != nullptr) *scrollYOut = st.value;
    ctx.ItemSize(outer.Size());
    ctx.NewLine();

    // The scrollbar is painted here, before content, so content scrolls over it
    // naturally; only its geometry is known now, which is fine because it does
    // not depend on the content height.
    if (showScrollbar && st.value > 0.5f) {
        const f32 bw = ctx.Var(StyleVarId::ScrollbarWidth);
        const Rect track(outer.max.x - bw - 1.0f, outer.min.y, outer.max.x - 1.0f, outer.max.y);
        const f32 fraction = Clamp01(h / Max(1.0f, h + st.value));
        const f32 thumbH = Max(28.0f, track.Height() * fraction);
        const f32 maxScroll = Max(1.0f, st.value);
        const f32 t = st.value / maxScroll;
        const Rect thumb(track.min.x, track.min.y + (track.Height() - thumbH) * t,
                         track.max.x, track.min.y + (track.Height() - thumbH) * t + thumbH);
        ctx.AddBox(track, Color::LerpC(ctx.Color(StyleColorId::Scrollbar),
                                        ctx.Color(StyleColorId::ScrollbarHover), 0.15f),
                   CornerRadius(bw * 0.5f));
        ctx.AddBox(thumb, ctx.Color(StyleColorId::ScrollbarHover), CornerRadius(bw * 0.5f));
    }
    return scrolled;
}

Rect ScrollAreaContent() {
    auto& s = CS().scrollContent;
    return s.empty() ? Rect() : s.back();
}

void EndScrollArea(Context& ctx) {
    auto& s = CS();
    if (!s.scrollContent.empty()) s.scrollContent.pop_back();
    ctx.EndLayout();
    ctx.PopClipRect();
}

// ===========================================================================
// Section - collapsible card
// ===========================================================================

bool BeginSection(Context& ctx, const char* title, bool* open, const char* subtitle, Icon icon) {
    const u32 baseId = ctx.GetID(title != nullptr ? title : "##section");
    WidgetState& st = ctx.State(baseId);

    bool isOpen = (open != nullptr) ? *open : st.opened;
    if (open != nullptr) *open = isOpen;
    st.opened = isOpen;

    const f32 headerH = ctx.Var(StyleVarId::SectionHeaderHeight);
    const f32 padX = ctx.Var(StyleVarId::SectionHeaderPaddingX);
    const f32 radius = ctx.Var(StyleVarId::SectionRadius);

    const Rect header(ctx.CursorPos(),
                      ctx.CursorPos() + Vec2(ctx.ContentRegionAvail().Width(), headerH));
    const ItemResult hr = ctx.ItemBehavior(baseId, header);
    if (hr.clicked) {
        isOpen = !isOpen;
        st.opened = isOpen;
        if (open != nullptr) *open = isOpen;
    }

    const f32 anim = ctx.AnimFloat(baseId ^ 0x9e9eu, isOpen ? 1.0f : 0.0f,
                                   ctx.Var(StyleVarId::ExpandRate));

    // Header surface. Diagonal radii so it nests into the rounded card below.
    ctx.AddBox(header,
               hr.hovered ? ctx.Color(StyleColorId::CardBgHover)
                          : ctx.Color(StyleColorId::SectionHeaderBg),
               CornerRadius::Diagonal(radius), 1.0f, ctx.Color(StyleColorId::CardBorder));

    Font* font = ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSize));
    f32 x = header.min.x + padX;
    if (icon != Icon::None) {
        const f32 gs = headerH * 0.46f;
        const Rect ir(x, header.GetCenter().y - gs * 0.5f, x + gs, header.GetCenter().y + gs * 0.5f);
        DrawIcon(ctx.MutableDrawList(), icon, ir, 1.7f, ctx.Color(StyleColorId::Accent));
        x = ir.max.x + ctx.Var(StyleVarId::Gap);
    }
    const f32 titleW = DrawTextC(ctx, Rect(x, header.min.y, header.max.x - 34.0f, header.max.y),
                                 title, font, ctx.Color(StyleColorId::Text), Align::Left);
    if (subtitle != nullptr && *subtitle != '\0') {
        DrawTextC(ctx, Rect(x + titleW + ctx.Var(StyleVarId::Gap), header.min.y,
                           header.max.x - 34.0f, header.max.y),
                  subtitle, ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall)),
                  ctx.Color(StyleColorId::TextMuted), Align::Left);
    }

    const f32 cs = headerH * 0.38f;
    const Rect chev(header.max.x - padX - cs, header.GetCenter().y - cs * 0.5f,
                    header.max.x - padX, header.GetCenter().y + cs * 0.5f);
    DrawIcon(ctx.MutableDrawList(), anim < 0.5f ? Icon::ChevronRight : Icon::ChevronDown, chev,
             1.8f,
             Color::LerpC(ctx.Color(StyleColorId::TextMuted), ctx.Color(StyleColorId::Accent), anim));

    ctx.ItemSize(header.Size());
    ctx.NewLine();

    // Everything from here is the body. The caller draws it unconditionally;
    // when the section is closed we clip it to nothing so the code path is the
    // same either way, so there is no "forgot to check" flicker.
    const Rect bodyTop(ctx.CursorPos().x, ctx.CursorPos().y, ctx.ContentRegionAvail().max.x, 0.0f);
    ctx.PushStyleVar(StyleVarId::FramePaddingX, ctx.Var(StyleVarId::SectionContentPadding));
    if (!isOpen) {
        ctx.PushClipRect(Rect(bodyTop.min.x, bodyTop.min.y, bodyTop.max.x, bodyTop.min.y));
    }

    auto& s = CS();
    s.sectionId.push_back(baseId);
    s.sectionTop.push_back(bodyTop);
    s.sectionClip.push_back(ctx.ClipRect());
    s.sectionCheckpoint.push_back(ctx.DrawCheckpoint());
    s.sectionOpen.push_back(isOpen);
    s.sectionY0.push_back(ctx.CursorPos().y);
    return isOpen;
}

void EndSection(Context& ctx) {
    auto& s = CS();
    if (s.sectionId.empty()) return;

    const u32 baseId = s.sectionId.back();
    const Rect top = s.sectionTop.back();
    const Rect clip = s.sectionClip.back();
    const u32 checkpoint = s.sectionCheckpoint.back();
    const bool isOpen = s.sectionOpen.back();
    const f32 y0 = s.sectionY0.back();
    s.sectionId.pop_back();
    s.sectionTop.pop_back();
    s.sectionClip.pop_back();
    s.sectionCheckpoint.pop_back();
    s.sectionOpen.pop_back();
    s.sectionY0.pop_back();

    if (!isOpen) ctx.PopClipRect();
    ctx.PopStyleVar(1);

    const f32 radius = ctx.Var(StyleVarId::SectionRadius);
    const f32 pad = ctx.Var(StyleVarId::SectionContentPadding);
    const f32 contentH = Max(0.0f, ctx.CursorPos().y - y0);

    // The card sits behind the body content, which was emitted after
    // `checkpoint`. InsertBackgroundBox splices it in at exactly the right
    // place in the index buffer, so nothing has to be drawn twice and the
    // caller never has to declare a height up front.
    if (contentH > 1.0f) {
        const Rect card(top.min.x - pad, y0 - pad, top.max.x + pad, y0 + contentH + pad * 0.5f);
        ctx.MutableDrawList().InsertBackgroundBox(
            card, CornerRadius(radius), ctx.Color(StyleColorId::CardBg), 1.0f,
            ctx.Color(StyleColorId::CardBorder), 0.0f, Color(0, 0, 0, 0), checkpoint, clip);
    }

    // Re-seat the cursor so a collapsed section takes no vertical space.
    ctx.SetCursorPos(Vec2(top.min.x, y0));
    ctx.ItemSize(Vec2(0.0f, isOpen ? contentH : 0.0f));
    ctx.NewLine();
}

// ===========================================================================
// Tab bar
// ===========================================================================

int TabBar(Context& ctx, const char* id, const char* const* tabs, int count, int* active,
           Rect* animatedIndicator, Icon* tabIcons) {
    const u32 baseId = ctx.GetID(id);
    Font* font = ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSize));

    const f32 h = ctx.Var(StyleVarId::TabHeight);
    const f32 padX = ctx.Var(StyleVarId::TabPaddingX);
    const f32 avail = ctx.ContentRegionAvail().Width();

    f32 widest = 0.0f;
    for (int i = 0; i < count; ++i) {
        f32 w = font->MeasureLine(tabs[i], std::strlen(tabs[i]));
        if (tabIcons != nullptr && tabIcons[i] != Icon::None) w += h * 0.5f;
        widest = Max(widest, w);
    }
    const f32 slot = (count > 0) ? Min(avail / static_cast<f32>(count), widest + padX * 2.0f) : 0.0f;

    const Rect bar(ctx.CursorPos(), ctx.CursorPos() + Vec2(slot * count, h));

    int cur = Clamp((active != nullptr) ? *active : 0, 0, Max(0, count - 1));
    const Vec2 pos = ctx.AnimVec2(baseId ^ 0x1234u, Vec2(slot * static_cast<f32>(cur), 0.0f),
                                  ctx.Var(StyleVarId::HoverRate) * 1.8f);

    const f32 ind = ctx.Var(StyleVarId::TabIndicatorThickness);
    if (animatedIndicator != nullptr) {
        *animatedIndicator = Rect(bar.min.x + pos.x, bar.max.y - ind, bar.min.x + pos.x + slot,
                                  bar.max.y);
    }

    int result = cur;
    for (int i = 0; i < count; ++i) {
        const u32 itemId = ctx.GetID(tabs[i]);
        const Rect itemRect(bar.min.x + slot * static_cast<f32>(i), bar.min.y,
                            bar.min.x + slot * static_cast<f32>(i + 1), bar.max.y);
        const ItemResult r = ctx.ItemBehavior(itemId, itemRect);

        if (r.hovered) {
            ctx.AddBox(Rect(itemRect.min.x + 4.0f, itemRect.min.y, itemRect.max.x - 4.0f,
                            itemRect.max.y),
                       ctx.Color(StyleColorId::AccentSoft),
                       CornerRadius(ctx.Var(StyleVarId::TabRadius)));
        }

        const bool isActive = (i == cur);
        if (tabIcons != nullptr && tabIcons[i] != Icon::None) {
            const f32 gs = h * 0.42f;
            const f32 tw = font->MeasureLine(tabs[i], std::strlen(tabs[i]));
            const f32 total = tw + gs + ctx.Var(StyleVarId::GapSmall);
            const f32 x0 = itemRect.GetCenter().x - total * 0.5f;
            const Rect ir(x0, itemRect.GetCenter().y - gs * 0.5f, x0 + gs,
                          itemRect.GetCenter().y + gs * 0.5f);
            DrawIcon(ctx.MutableDrawList(), tabIcons[i], ir, 1.7f,
                     isActive ? ctx.Color(StyleColorId::Accent) : ctx.Color(StyleColorId::TextDim));
        }

        DrawTextC(ctx, itemRect, tabs[i], font,
                  isActive ? ctx.Color(StyleColorId::Text) : ctx.Color(StyleColorId::TextDim),
                  Align::Center);
        if (r.clicked) result = i;
    }

    ctx.AddBox(Rect(bar.min.x + pos.x, bar.max.y - ind, bar.min.x + pos.x + slot, bar.max.y),
               ctx.Color(StyleColorId::Accent), CornerRadius(ind * 0.5f));

    ctx.ItemSize(bar.Size());
    ctx.NewLine();
    if (active != nullptr) *active = result;
    return result;
}

// ===========================================================================
// Sidebar
// ===========================================================================

int Sidebar(Context& ctx, const char* id, const Icon* icons, int count, int* selected, f32 width,
            const int* badges) {
    const u32 baseId = ctx.GetID(id);
    const f32 w = width > 0.0f ? width : ctx.Var(StyleVarId::SidebarWidth);
    const f32 itemSize = ctx.Var(StyleVarId::SidebarItemSize);
    const f32 gap = ctx.Var(StyleVarId::SidebarItemGap);
    const f32 pad = (w - itemSize) * 0.5f;

    const Vec2 avail = ctx.ContentRegionAvail();
    const Rect rail(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, avail.y));
    ctx.AddBox(rail, ctx.Color(StyleColorId::SidebarBg));

    int cur = Clamp((selected != nullptr) ? *selected : 0, 0, Max(0, count - 1));
    int result = cur;

    // Vertical centring when the rail is taller than its contents.
    const f32 itemsH = itemSize * count + gap * Max(0, count - 1);
    f32 y = rail.min.y;
    if (rail.Height() > itemsH) y = rail.min.y + (rail.Height() - itemsH) * 0.5f;

    for (int i = 0; i < count; ++i) {
        const u32 itemId = ctx.GetID(icons[i]);
        const Rect itemRect(rail.min.x + pad, y, rail.min.x + pad + itemSize, y + itemSize);
        const ItemResult r = ctx.ItemBehavior(itemId, itemRect);

        const bool active = (i == cur);
        if (active) {
            ctx.AddBox(itemRect, ctx.Color(StyleColorId::AccentSoft),
                       CornerRadius(ctx.Var(StyleVarId::SidebarItemRadius)));
        } else if (r.hovered) {
            ctx.AddBox(itemRect, ctx.Color(StyleColorId::CardBgHover).WithAlpha(0.7f),
                       CornerRadius(ctx.Var(StyleVarId::SidebarItemRadius)));
        }

        const f32 gs = itemSize * 0.50f;
        const Rect ir(itemRect.GetCenter() - Vec2(gs * 0.5f), itemRect.GetCenter() + Vec2(gs * 0.5f));
        DrawIcon(ctx.MutableDrawList(), icons[i], ir, 1.9f,
                 active ? ctx.Color(StyleColorId::Accent) : ctx.Color(StyleColorId::TextDim));

        if (badges != nullptr && badges[i] > 0) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", badges[i]);
            Font* bf = ctx.FontAt(ctx.Var(StyleVarId::TextSizeTiny));
            const f32 bw = Max(14.0f, bf->MeasureLine(buf, std::strlen(buf)) + 8.0f);
            const Rect chip(itemRect.max.x - bw * 0.4f, itemRect.min.y, itemRect.max.x + bw * 0.6f,
                            itemRect.min.y + 14.0f);
            ctx.AddBox(chip, ctx.Color(StyleColorId::Accent), CornerRadius(7.0f));
            DrawTextC(ctx, chip, buf, bf, ctx.Color(StyleColorId::OnAccent), Align::Center);
        }

        if (r.clicked) result = i;
        y += itemSize + gap;
    }

    // The rail is a layout column of its own so callers can start the content
    // region to its right without doing arithmetic.
    ctx.ItemSize(Vec2(w, rail.Height()));
    ctx.NewLine();
    if (selected != nullptr) *selected = result;
    return result;
}

// ===========================================================================
// Window
// ===========================================================================

namespace {

bool WindowImpl(Context& ctx, const char* id, const char* title, const Rect& bounds, bool* open,
                Icon appIcon) {
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    if (open != nullptr && !*open) return false;

    ctx.PushZ();  // a window always owns hover over whatever is beneath it

    const f32 radius = ctx.Var(StyleVarId::WindowRadius);
    const f32 borderW = ctx.Var(StyleVarId::WindowBorderWidth);
    const f32 shadow = ctx.Var(StyleVarId::WindowShadowSize);

    // Shadow.
    ctx.AddBox(bounds, ctx.Color(StyleColorId::Shadow), CornerRadius(radius), 0.0f,
               Color(0, 0, 0, 0), shadow, ctx.Color(StyleColorId::Shadow));
    // Body.
    ctx.AddBox(bounds, ctx.Color(StyleColorId::WindowBg), CornerRadius(radius), borderW,
               ctx.Color(StyleColorId::WindowBorder));

    const f32 titleH = ctx.Var(StyleVarId::TitleBarHeight);
    const Rect titleRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y + titleH);
    // Title bar is a rectangle with the window's top corners only.
    ctx.AddBox(Rect(titleRect.min.x, titleRect.min.y, titleRect.max.x, titleRect.max.y),
               ctx.Color(StyleColorId::TitleBarBg), CornerRadius::Diagonal(radius), 0.0f,
               Color(0, 0, 0, 0));

    Font* font = ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSizeTitle));
    f32 x = titleRect.min.x + ctx.Var(StyleVarId::WindowPadding) + ctx.Var(StyleVarId::Gap);
    if (appIcon != Icon::None) {
        const f32 gs = 20.0f;
        const Rect ir(x, titleRect.GetCenter().y - gs * 0.5f, x + gs,
                      titleRect.GetCenter().y + gs * 0.5f);
        ctx.AddBox(ir, ctx.Color(StyleColorId::AccentSoft), CornerRadius(6.0f), 1.5f,
                   ctx.Color(StyleColorId::Accent));
        DrawIcon(ctx.MutableDrawList(), appIcon, ir, 1.8f, ctx.Color(StyleColorId::Accent));
        x = ir.max.x + ctx.Var(StyleVarId::Gap);
    }
    DrawTextC(ctx, Rect(x, titleRect.min.y, titleRect.max.x - 120.0f, titleRect.max.y), title,
              font, ctx.Color(StyleColorId::Text), Align::Left);

    // Close button.
    if (open != nullptr) {
        const f32 bs = 30.0f;
        const Rect btn(titleRect.max.x - bs - ctx.Var(StyleVarId::WindowPadding) - 8.0f,
                       titleRect.GetCenter().y - bs * 0.5f,
                       titleRect.max.x - ctx.Var(StyleVarId::WindowPadding) - 8.0f,
                       titleRect.GetCenter().y + bs * 0.5f);
        const u32 closeId = ctx.GetID("##close");
        const ItemResult r = ctx.ItemBehavior(closeId, btn);
        ctx.AddBox(btn, r.hovered ? ctx.Color(StyleColorId::DangerSoft) : Color(0, 0, 0, 0),
                   CornerRadius(8.0f));
        const f32 gs = bs * 0.44f;
        DrawIcon(ctx.MutableDrawList(), Icon::Close,
                 Rect(btn.GetCenter() - Vec2(gs * 0.5f), btn.GetCenter() + Vec2(gs * 0.5f)),
                 1.8f, r.hovered ? ctx.Color(StyleColorId::Danger) : ctx.Color(StyleColorId::TextDim));
        if (r.clicked) {
            *open = false;
            st.opened = false;
        }
    }

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    // Content region: everything below the title bar, inside the padding.
    const f32 pad = ctx.Var(StyleVarId::WindowPadding);
    const Rect content(bounds.min.x + pad, titleRect.max.y + pad * 0.5f, bounds.max.x - pad,
                       bounds.max.y - pad);
    ctx.BeginLayout(content);

    CS().windowRect.push_back(bounds);
    return true;
}

} // namespace

bool BeginWindowAt(Context& ctx, const char* id, const char* title, const Rect& bounds, bool* open,
                   Icon appIcon) {
    return WindowImpl(ctx, id, title, bounds, open, appIcon);
}

bool BeginWindow(Context& ctx, const char* id, const char* title, const Vec2& size, bool* open,
                 Icon appIcon) {
    const Rect display(0, 0, ctx.Frame().displaySize.x, ctx.Frame().displaySize.y);
    const Rect bounds(display.GetCenter() - size * 0.5f, display.GetCenter() + size * 0.5f);
    return WindowImpl(ctx, id, title, bounds, open, appIcon);
}

void EndWindow(Context& ctx) {
    auto& s = CS();
    if (!s.windowRect.empty()) s.windowRect.pop_back();
    ctx.EndLayout();
    ctx.PopZ();
}

// ===========================================================================
// Rows
// ===========================================================================

bool Row(Context& ctx, const char* label, const RowOptions& opt,
         bool (*drawControl)(Context&, const Rect&, void*), void* userData) {
    const u32 baseId = ctx.GetID(label != nullptr ? label : "##row");
    const f32 h = opt.height > 0.0f ? opt.height : ctx.Var(StyleVarId::RowHeight);
    const f32 padX = ctx.Var(StyleVarId::RowPaddingX);
    const Rect bounds(ctx.CursorPos(),
                      ctx.CursorPos() + Vec2(ctx.ContentRegionAvail().Width(), h));

    const ItemResult r = ctx.ItemBehavior(baseId, bounds, ItemFlags::NoInteraction);
    if (opt.highlightOnHover && r.hoveredExact) {
        ctx.AddBox(bounds, ctx.Color(StyleColorId::CardBgHover).WithAlpha(0.55f),
                   CornerRadius(6.0f));
    }

    f32 x = bounds.min.x + padX;
    if (opt.icon != Icon::None) {
        const f32 gs = h * 0.50f;
        const Rect ir(x, bounds.GetCenter().y - gs * 0.5f, x + gs, bounds.GetCenter().y + gs * 0.5f);
        DrawIcon(ctx.MutableDrawList(), opt.icon, ir, 1.7f, ctx.Color(StyleColorId::TextDim));
        x = ir.max.x + ctx.Var(StyleVarId::Gap);
    }
    if (opt.dotColor.a > 0.0f) {
        const f32 d = 7.0f;
        const Rect dot(x, bounds.GetCenter().y - d * 0.5f, x + d, bounds.GetCenter().y + d * 0.5f);
        ctx.AddCircle(dot, opt.dotColor);
        x = dot.max.x + ctx.Var(StyleVarId::Gap);
    }

    // Right-hand side first so we know how much width the label gets.
    f32 controlX = bounds.max.x - padX;
    if (opt.overflowButton) {
        const f32 bs = h * 0.72f;
        const Rect btn(controlX - bs, bounds.GetCenter().y - bs * 0.5f, controlX,
                       bounds.GetCenter().y + bs * 0.5f);
        const ItemResult br = ctx.ItemBehavior(ctx.GetID("##rowmenu"), btn);
        if (br.hovered) {
            ctx.AddBox(btn, ctx.Color(StyleColorId::CardBgHover), CornerRadius(6.0f));
        }
        const f32 gs = bs * 0.42f;
        DrawIcon(ctx.MutableDrawList(), Icon::More,
                 Rect(btn.GetCenter() - Vec2(gs * 0.5f), btn.GetCenter() + Vec2(gs * 0.5f)),
                 1.7f, ctx.Color(StyleColorId::TextMuted));
        controlX = btn.min.x - ctx.Var(StyleVarId::GapSmall);
    }

    // The control's own width is whatever it wants; give it the remainder.
    const Rect controlRect(x + ctx.Var(StyleVarId::GapLargeX), bounds.min.y,
                           controlX, bounds.max.y);

    f32 labelMaxW = controlRect.min.x - x - ctx.Var(StyleVarId::GapLargeX);
    if (label != nullptr && *label != '\0') {
        Font* font = ctx.FontAt(opt.labelFontSize > 0.0f ? opt.labelFontSize
                                                         : ctx.Var(StyleVarId::TextSize));
        DrawTextC(ctx, Rect(x, bounds.min.y, x + labelMaxW, bounds.max.y), label, font,
                  ctx.Color(StyleColorId::Text), Align::Left);
    }

    bool changed = false;
    if (drawControl != nullptr) {
        // The control draws itself at the row's right edge but must not disturb
        // the outer layout, so the cursor is put back afterwards.
        const Context::CursorState saved = ctx.SaveCursor();
        changed = drawControl(ctx, controlRect, userData);
        ctx.RestoreCursor(saved);
    }

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (opt.separatorAfter) {
        const f32 inset = ctx.Var(StyleVarId::SeparatorInset);
        const f32 t = ctx.Var(StyleVarId::SeparatorThickness);
        ctx.AddLine(Vec2(bounds.min.x + inset, bounds.max.y),
                    Vec2(bounds.max.x - inset, bounds.max.y), t,
                    ctx.Color(StyleColorId::Separator));
    }
    return changed;
}

namespace {
struct ToggleRef { bool* value; bool changed = false; };
bool RowToggleDraw(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<ToggleRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::ToggleHeight);
    const f32 w = ctx.Var(StyleVarId::ToggleWidth);
    // Right-align inside the control rect.
    ctx.SetCursorPos(Vec2(r.max.x - w, r.GetCenter().y - h * 0.5f));
    ref->changed = ToggleSwitch(ref->value);
    return ref->changed;
}
struct SliderRef { f32* value; f32 vmin; f32 vmax; const char* fmt; bool changed = false; };
bool RowSliderDraw(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<SliderRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::SliderHeight);
    ctx.SetCursorPos(Vec2(r.min.x, r.GetCenter().y - h * 0.5f));
    const f32 avail = r.Width();
    // Keep the thumb a sane size even in a very wide row.
    ctx.PushStyleVar(StyleVarId::SliderThumbRadius,
                     Clamp(ctx.Var(StyleVarId::SliderThumbRadius), 5.0f, Max(5.0f, avail * 0.35f)));
    ref->changed = SliderFloat("", ref->value, ref->vmin, ref->vmax, ref->fmt);
    ctx.PopStyleVar(1);
    return ref->changed;
}
struct ComboRef { const char* id; const char* const* options; int count; int* selected;
                   bool changed = false; };
bool RowComboDraw(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<ComboRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::FrameHeight);
    const f32 w = Min(160.0f, Max(110.0f, r.Width() * 0.5f));
    ctx.SetCursorPos(Vec2(r.max.x - w, r.GetCenter().y - h * 0.5f));
    ref->changed = Combo(ref->id, ref->options, ref->count, ref->selected, w) >= 0;
    return ref->changed;
}
struct ButtonRef { const char* label; ButtonKind kind; Icon icon; IconSlot slot;
                   bool changed = false; };
bool RowButtonDraw(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<ButtonRef*>(ud);
    const f32 h = Min(ctx.Var(StyleVarId::ButtonHeight), r.Height());
    const f32 w = 120.0f;
    ctx.SetCursorPos(Vec2(r.max.x - w, r.GetCenter().y - h * 0.5f));
    ref->changed = ButtonWithIcon(ref->label, ref->icon, ref->kind, ref->slot, Vec2(w, h));
    return ref->changed;
}
} // namespace

bool RowToggle(const char* label, bool* value, bool) {
    Context& ctx = Context::Get();
    ToggleRef ref{value};
    RowOptions opt;
    Row(ctx, label, opt, RowToggleDraw, &ref);
    return ref.changed;
}

bool RowSlider(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt, bool) {
    Context& ctx = Context::Get();
    SliderRef ref{value, vmin, vmax, fmt};
    RowOptions opt;
    Row(ctx, label, opt, RowSliderDraw, &ref);
    return ref.changed;
}

bool RowCombo(const char* id, const char* const* options, int count, int* selected) {
    Context& ctx = Context::Get();
    ComboRef ref{id, options, count, selected};
    RowOptions opt;
    Row(ctx, nullptr, opt, RowComboDraw, &ref);
    return ref.changed;
}

bool RowButton(const char* label, const char* buttonLabel, ButtonKind kind, Icon icon,
               IconSlot slot) {
    Context& ctx = Context::Get();
    ButtonRef ref{buttonLabel, kind, icon, slot};
    RowOptions opt;
    Row(ctx, label, opt, RowButtonDraw, &ref);
    return ref.changed;
}

void InfoRow(Context& ctx, const char* label, const char* value, Color dotColor, Icon icon) {
    const f32 h = ctx.Var(StyleVarId::RowHeight);
    const f32 padX = ctx.Var(StyleVarId::RowPaddingX);
    const Rect bounds(ctx.CursorPos(),
                      ctx.CursorPos() + Vec2(ctx.ContentRegionAvail().Width(), h));

    f32 x = bounds.min.x + padX;
    if (icon != Icon::None) {
        const f32 gs = h * 0.48f;
        const Rect ir(x, bounds.GetCenter().y - gs * 0.5f, x + gs, bounds.GetCenter().y + gs * 0.5f);
        DrawIcon(ctx.MutableDrawList(), icon, ir, 1.7f, ctx.Color(StyleColorId::TextDim));
        x = ir.max.x + ctx.Var(StyleVarId::Gap);
    }
    if (dotColor.a > 0.0f) {
        const f32 d = 7.0f;
        const Rect dot(x, bounds.GetCenter().y - d * 0.5f, x + d, bounds.GetCenter().y + d * 0.5f);
        ctx.AddCircle(dot, dotColor);
        x = dot.max.x + ctx.Var(StyleVarId::Gap);
    }

    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSize));
    DrawTextC(ctx, Rect(x, bounds.min.y, bounds.GetCenter().x, bounds.max.y), label, font,
              ctx.Color(StyleColorId::TextDim), Align::Left);
    DrawTextC(ctx, Rect(bounds.GetCenter().x, bounds.min.y, bounds.max.x - padX, bounds.max.y),
              value, ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSizeSmall)),
              ctx.Color(StyleColorId::Text), Align::Right);

    const f32 inset = ctx.Var(StyleVarId::SeparatorInset);
    ctx.AddLine(Vec2(bounds.min.x + inset, bounds.max.y),
                Vec2(bounds.max.x - inset, bounds.max.y), ctx.Var(StyleVarId::SeparatorThickness),
                ctx.Color(StyleColorId::Separator));

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();
}

} // namespace argon::ui