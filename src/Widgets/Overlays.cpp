// ArgonUI - src/Widgets/Overlays.cpp
// Modals, context menus, badges, progress bars, breadcrumbs, grids and lists.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "argonui/Widgets/Widgets.h"

namespace argon::ui {
namespace {
enum class Align { Left, Center, Right };

f32 DrawTextO(Context& ctx, const Rect& bounds, const char* text, Font* font, const Color& color,
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

struct OverlayStack {
    std::vector<Rect> header, body, footer, content, open;
    std::vector<bool> wasOpen;
    std::vector<Rect> checkpointClip;
    std::vector<u32> checkpoint;
};
OverlayStack& OS() { static OverlayStack s; return s; }

} // namespace

// Progress bar / badge / breadcrumbs

void ProgressBar(f32 fraction, const char* label, const Color& color) {
    Context& ctx = Context::Get();
    const f32 h = 6.0f;
    const f32 w = ctx.ContentRegionAvail().Width();
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));
    const f32 f = Clamp01(fraction);

    ctx.AddBox(bounds, ctx.Color(StyleColorId::SliderRail), CornerRadius(h * 0.5f));
    if (f > 0.001f) {
        const Color c = color.a > 0.0f ? color : ctx.Color(StyleColorId::Accent);
        const Rect fill(bounds.min.x, bounds.min.y, bounds.min.x + w * f, bounds.max.y);
        ctx.AddBox(fill, c, CornerRadius(h * 0.5f));
    }
    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (label != nullptr && *label != '\0') {
        DrawTextO(ctx, Rect(bounds.min.x, bounds.max.y + 4.0f, bounds.max.x, bounds.max.y + 18.0f),
                  label, ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall)),
                  ctx.Color(StyleColorId::TextMuted), Align::Left);
        ctx.ItemSize(Vec2(w, 18.0f));
        ctx.NewLine();
    }
}

Vec2 BadgeSize(const char* text, const Color&) {
    Context& ctx = Context::Get();
    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSizeTiny));
    const f32 h = ctx.Var(StyleVarId::BadgeHeight);
    const f32 pad = ctx.Var(StyleVarId::BadgePaddingX);
    return Vec2(font->MeasureLine(text, std::strlen(text)) + pad * 2.0f, h);
}

void Badge(const char* text, const Color& color) {
    Context& ctx = Context::Get();
    const Color tint = color.a > 0.0f ? color : ctx.Color(StyleColorId::AccentSoft);
    const Vec2 size = BadgeSize(text, tint);
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + size);
    ctx.AddBox(bounds, tint, CornerRadius(ctx.Var(StyleVarId::BadgeRadius)));
    DrawTextO(ctx, bounds, text, ctx.FontAt(ctx.Var(StyleVarId::TextSizeTiny)),
              color.a > 0.0f ? color : ctx.Color(StyleColorId::Accent), Align::Center);
    ctx.ItemSize(size);
    ctx.NewLine();
}

void Breadcrumbs(const char* const* items, int count, int* hoveredIndex) {
    Context& ctx = Context::Get();
    Font* font = ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall));
    const f32 h = 26.0f;

    // Total width first so the trailing filler bar lines up.
    f32 total = 0.0f;
    for (int i = 0; i < count; ++i) {
        total += font->MeasureLine(items[i], std::strlen(items[i]));
        if (i + 1 < count) total += 26.0f;
    }
    const f32 avail = ctx.ContentRegionAvail().Width();

    f32 x = ctx.CursorPos().x;
    if (hoveredIndex != nullptr) *hoveredIndex = -1;

    for (int i = 0; i < count; ++i) {
        const f32 w = font->MeasureLine(items[i], std::strlen(items[i]));
        const Rect item(x, ctx.CursorPos().y, x + w, ctx.CursorPos().y + h);
        const ItemResult r = ctx.ItemBehavior(ctx.GetID(items[i]), item);

        const Color fg = (i == count - 1) ? ctx.Color(StyleColorId::Text)
                                          : ctx.Color(StyleColorId::TextDim);
        DrawTextO(ctx, item, items[i], font, r.hovered ? ctx.Color(StyleColorId::Text) : fg,
                  Align::Left);
        if (r.hovered && hoveredIndex != nullptr) *hoveredIndex = i;

        x += w;
        if (i + 1 < count) {
            const f32 gs = h * 0.36f;
            const Rect sep(x + 6.0f, item.GetCenter().y - gs * 0.5f, x + 6.0f + gs,
                           item.GetCenter().y + gs * 0.5f);
            DrawIcon(ctx.MutableDrawList(), Icon::ChevronRight, sep, 1.5f,
                     ctx.Color(StyleColorId::TextMuted));
            x += 26.0f;
        }
    }

    // Filler so the row reads as a toolbar rather than floating text.
    if (x < ctx.CursorPos().x + avail) {
        ctx.Dummy(Vec2(avail - (x - ctx.CursorPos().x), h));
    } else {
        ctx.ItemSize(Vec2(x - ctx.CursorPos().x, h));
        ctx.NewLine();
    }
}

// Grid

void Grid(Context& ctx, int columns, f32 cellSize, f32 spacing,
          bool (*drawCell)(Context&, const Rect&, int, void*), int count, void* userData) {
    if (columns < 1) columns = 1;
    const f32 pad = ctx.Var(StyleVarId::GridCellPadding);
    const f32 radius = ctx.Var(StyleVarId::GridCellRadius);
    const f32 totalW = columns * cellSize + (columns - 1) * spacing;

    for (int i = 0; i < count; ++i) {
        const int col = i % columns;
        const int row = i / columns;
        const Vec2 origin = ctx.CursorPos();
        const Rect cell(origin.x + col * (cellSize + spacing), origin.y + row * (cellSize + spacing),
                        origin.x + col * (cellSize + spacing) + cellSize,
                        origin.y + row * (cellSize + spacing) + cellSize);

        // Tile background behind the cell contents.
        const u32 cellId = ctx.GetID("gridcell");
        const ItemResult r = ctx.ItemBehavior(cellId, cell);
        ctx.AddBox(cell,
                   r.hovered ? ctx.Color(StyleColorId::CardBgHover) : ctx.Color(StyleColorId::CardBg),
                   CornerRadius(radius), 1.0f, ctx.Color(StyleColorId::CardBorder));

        if (drawCell != nullptr) {
            const Context::CursorState saved = ctx.SaveCursor();
            const Rect inner(cell.min.x + pad, cell.min.y + pad, cell.max.x - pad,
                             cell.max.y - pad);
            ctx.SetCursorPos(inner.min);
            drawCell(ctx, inner, i, userData);
            ctx.RestoreCursor(saved);
        }

        (void)row;
    }

    const int rows = (count + columns - 1) / columns;
    ctx.ItemSize(Vec2(totalW, rows * cellSize + Max(0, rows - 1) * spacing));
    ctx.NewLine();
}

// List

int List(Context& ctx, const char* id, const ListItem* items, int count, int* selected,
         f32 rowHeight) {
    const f32 h = rowHeight > 0.0f ? rowHeight : ctx.Var(StyleVarId::ListItemHeight);
    const f32 padX = ctx.Var(StyleVarId::RowPaddingX);
    const f32 radius = ctx.Var(StyleVarId::ListItemRadius);
    const f32 w = ctx.ContentRegionAvail().Width();

    int cur = (selected != nullptr) ? *selected : -1;
    int result = cur;

    for (int i = 0; i < count; ++i) {
        const ListItem& it = items[i];
        const u32 itemId = ctx.GetID(it.label != nullptr ? it.label : "##item");
        const Rect row(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));
        const ItemResult r =
            ctx.ItemBehavior(itemId, row, it.disabled ? ItemFlags::NoInteraction : ItemFlags::None);

        if (i == cur) {
            ctx.AddBox(row, ctx.Color(StyleColorId::Selection), CornerRadius(radius));
        } else if (r.hovered && !it.disabled) {
            ctx.AddBox(row, ctx.Color(StyleColorId::CardBgHover), CornerRadius(radius));
        }

        f32 x = row.min.x + padX;
        if (it.textureId != 0) {
            const f32 s = h - 10.0f;
            const Rect tr(row.min.x + 5.0f, row.GetCenter().y - s * 0.5f,
                          row.min.x + 5.0f + s, row.GetCenter().y + s * 0.5f);
            ctx.AddImage(tr, it.textureId, Color(1, 1, 1, 1), CornerRadius(5.0f));
            x = tr.max.x + padX;
        } else if (it.icon != Icon::None) {
            const f32 gs = h * 0.50f;
            const Rect ir(x, row.GetCenter().y - gs * 0.5f, x + gs, row.GetCenter().y + gs * 0.5f);
            DrawIcon(ctx.MutableDrawList(), it.icon, ir, 1.7f, ctx.Color(StyleColorId::TextDim));
            x = ir.max.x + padX;
        } else if (it.dotColor.a > 0.0f) {
            const f32 d = 7.0f;
            const Rect dot(x, row.GetCenter().y - d * 0.5f, x + d, row.GetCenter().y + d * 0.5f);
            ctx.AddCircle(dot, it.dotColor);
            x = dot.max.x + padX;
        }

        const Color fg = it.disabled ? ctx.Color(StyleColorId::TextDisabled)
                                     : ctx.Color(StyleColorId::Text);
        f32 detailX = row.max.x - padX;
        if (it.detail != nullptr && *it.detail != '\0') {
            const f32 dw = ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall))->MeasureLine(
                it.detail, std::strlen(it.detail));
            DrawTextO(ctx, Rect(row.GetCenter().x, row.min.y, row.max.x - padX, row.max.y),
                      it.detail, ctx.FontAt(ctx.Var(StyleVarId::TextSizeSmall)),
                      ctx.Color(StyleColorId::TextMuted), Align::Right);
            detailX -= dw + padX;
        }
        DrawTextO(ctx, Rect(x, row.min.y, detailX, row.max.y), it.label,
                  ctx.FontAt(ctx.Var(StyleVarId::TextSize)), fg, Align::Left);

        if (r.clicked && !it.disabled) result = i;

        ctx.ItemSize(row.Size());
        ctx.NewLine();
    }
    (void)id;
    if (selected != nullptr) *selected = result;
    return result;
}

// Modal

bool BeginModal(Context& ctx, const char* id, const char* title, const Vec2& size, bool* open,
                Icon icon, const char* closeLabel) {
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    bool isOpen = (open != nullptr) ? *open : st.opened;
    // Escape closes.
    if (isOpen && ctx.Input().IsKeyPressed(Key_Escape)) isOpen = false;
    if (open != nullptr) *open = isOpen;
    st.opened = isOpen;
    if (!isOpen) return false;

    ctx.PushZ();          // above any popup that might be open underneath
    ctx.PushInputLock();  // and input is locked to the modal

    const Vec2 display = ctx.Frame().displaySize;
    Vec2 s = size;
    s.x = Max(s.x, ctx.Var(StyleVarId::ModalMinWidth));
    s.y = Max(s.y, ctx.Var(StyleVarId::ModalMinHeight));
    s.x = Min(s.x, display.x - 40.0f);
    s.y = Min(s.y, display.y - 40.0f);

    // Entrance: fade + a short rise, driven by the open flag.
    const f32 anim = ctx.AnimTo(baseId ^ 0xa11aull, 1.0f, 0.0f);
    const Rect frame(display.GetCenter() - s * 0.5f + Vec2(0.0f, (1.0f - anim) * 14.0f),
                     display.GetCenter() + s * 0.5f + Vec2(0.0f, (1.0f - anim) * 14.0f));

    // Backdrop. Registered as an item so it wins hover over everything under it.
    const Rect backdrop(0, 0, display.x, display.y);
    const ItemResult br =
        ctx.ItemBehavior(baseId ^ 0xbaccu, backdrop, ItemFlags::NoInteraction);

    ctx.AddBox(backdrop, ctx.Color(StyleColorId::Backdrop).WithAlpha(ctx.Color(StyleColorId::Backdrop).a * anim));

    const f32 radius = ctx.Var(StyleVarId::ModalRadius);
    ctx.AddBox(frame, ctx.Color(StyleColorId::Shadow), CornerRadius(radius), 0.0f, Color(0, 0, 0, 0),
               26.0f, ctx.Color(StyleColorId::Shadow));
    ctx.AddBox(frame, ctx.Color(StyleColorId::WindowBg).WithAlpha(anim), CornerRadius(radius), 1.0f,
               ctx.Color(StyleColorId::WindowBorder));

    // Header.
    const f32 headerH = ctx.Var(StyleVarId::ModalHeaderHeight);
    const Rect header(frame.min.x, frame.min.y, frame.max.x, frame.min.y + headerH);
    ctx.AddBox(header, ctx.Color(StyleColorId::TitleBarBg).WithAlpha(anim),
               CornerRadius::Diagonal(radius));

    f32 tx = header.min.x + ctx.Var(StyleVarId::ModalPadding);
    if (icon != Icon::None) {
        const f32 gs = 20.0f;
        const Rect ir(tx, header.GetCenter().y - gs * 0.5f, tx + gs,
                      header.GetCenter().y + gs * 0.5f);
        DrawIcon(ctx.MutableDrawList(), icon, ir, 1.9f, ctx.Color(StyleColorId::Accent));
        tx = ir.max.x + ctx.Var(StyleVarId::Gap);
    }
    DrawTextO(ctx, Rect(tx, header.min.y, header.max.x - 80.0f, header.max.y), title,
              ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSizeTitle)), ctx.Color(StyleColorId::Text),
              Align::Left);

    const f32 bs = 28.0f;
    const Rect close(header.max.x - bs - ctx.Var(StyleVarId::ModalPadding),
                     header.GetCenter().y - bs * 0.5f,
                     header.max.x - ctx.Var(StyleVarId::ModalPadding),
                     header.GetCenter().y + bs * 0.5f);
    const ItemResult cr = ctx.ItemBehavior(baseId ^ 0xc105u, close);
    if (cr.hovered) ctx.AddBox(close, ctx.Color(StyleColorId::CardBgHover), CornerRadius(7.0f));
    const f32 gs = bs * 0.42f;
    DrawIcon(ctx.MutableDrawList(), Icon::Close,
             Rect(close.GetCenter() - Vec2(gs * 0.5f), close.GetCenter() + Vec2(gs * 0.5f)),
             1.7f, cr.hovered ? ctx.Color(StyleColorId::Danger) : ctx.Color(StyleColorId::TextDim));
    if (cr.clicked) {
        isOpen = false;
        st.opened = false;
        if (open != nullptr) *open = false;
    }

    // Footer with optional confirm / cancel.
    f32 footerH = 0.0f;
    if (closeLabel != nullptr) {
        footerH = ctx.Var(StyleVarId::ModalFooterHeight);
        const Rect footer(frame.min.x, frame.max.y - footerH, frame.max.x, frame.max.y);
        ctx.AddBox(footer, ctx.Color(StyleColorId::TitleBarBg).WithAlpha(anim),
                   CornerRadius::Diagonal(radius));

        const f32 bw = 108.0f;
        const Rect ok(footer.max.x - ctx.Var(StyleVarId::ModalPadding) - bw,
                      footer.GetCenter().y - 17.0f, footer.max.x - ctx.Var(StyleVarId::ModalPadding),
                      footer.GetCenter().y + 17.0f);
        const Rect cancel(ok.min.x - bw - ctx.Var(StyleVarId::Gap), ok.min.y, ok.min.x - ctx.Var(StyleVarId::Gap), ok.max.y);

        const Context::CursorState saved = ctx.SaveCursor();
        ctx.SetCursorPos(cancel.min);
        if (Button("Cancel", ButtonKind::Secondary, Vec2(bw, 34.0f))) {
            isOpen = false;
            st.opened = false;
            if (open != nullptr) *open = false;
        }
        ctx.SetCursorPos(ok.min);
        if (Button(closeLabel, ButtonKind::Primary, Vec2(bw, 34.0f))) {
            isOpen = false;
            st.opened = false;
            if (open != nullptr) *open = false;
        }
        ctx.RestoreCursor(saved);
    }

    // Body.
    const f32 pad = ctx.Var(StyleVarId::ModalPadding);
    const Rect body(header.min.x + pad, header.max.y + pad * 0.5f, header.max.x - pad,
                    frame.max.y - footerH - pad * 0.5f);

    auto& s = OS();
    s.header.push_back(header);
    s.body.push_back(body);
    s.footer.push_back(Rect(frame.min.x, frame.max.y - footerH, frame.max.x, frame.max.y));
    s.wasOpen.push_back(isOpen);
    s.checkpointClip.push_back(ctx.ClipRect());
    s.checkpoint.push_back(ctx.DrawCheckpoint());

    ctx.BeginLayout(body);
    (void)br;
    return isOpen;
}

void EndModal(Context& ctx) {
    auto& s = OS();
    if (s.body.empty()) return;
    s.header.pop_back();
    s.footer.pop_back();
    const Rect body = s.body.back();
    s.body.pop_back();
    s.wasOpen.pop_back();
    const Rect clip = s.checkpointClip.back();
    s.checkpointClip.pop_back();
    const u32 checkpoint = s.checkpoint.back();
    s.checkpoint.pop_back();

    const f32 contentH = Max(0.0f, ctx.CursorPos().y - body.min.y);
    ctx.EndLayout();

    // Surface behind the body content, sized to what the body actually used.
    if (contentH > 1.0f) {
        const f32 radius = ctx.Var(StyleVarId::ModalRadius);
        const Rect card(body.min.x - 10.0f, body.min.y - 8.0f, body.max.x + 10.0f,
                        body.min.y + contentH + 8.0f);
        ctx.MutableDrawList().InsertBackgroundBox(card, CornerRadius(10.0f),
                                                  ctx.Color(StyleColorId::CardBg), 1.0f,
                                                  ctx.Color(StyleColorId::CardBorder), 0.0f,
                                                  Color(0, 0, 0, 0), checkpoint, clip);
    }

    ctx.PopInputLock();
    ctx.PopZ();
}

Rect ModalContent() {
    auto& s = OS();
    return s.body.empty() ? Rect() : s.body.back();
}

Rect ModalFooter() {
    auto& s = OS();
    return s.footer.empty() ? Rect() : s.footer.back();
}

Rect ModalHeader() {
    auto& s = OS();
    return s.header.empty() ? Rect() : s.header.back();
}

// Context menu

int ContextMenu(Context& ctx, const char* id, const char* const* labels, int count,
                const Icon* icons, bool* open) {
    const u32 baseId = ctx.GetID(id);
    WidgetState& st = ctx.State(baseId);

    if (ctx.Input().IsMouseClicked(Mouse_Right) && !ctx.IsInputLocked()) {
        st.opened = !st.opened;
        if (st.opened) {
            st.vec = ctx.Input().mousePos;  // anchor at the click
        }
    }
    bool isOpen = (open != nullptr) ? *open : st.opened;

    int pick = PopupList(ctx, id, Rect(st.vec, st.vec), labels, count, nullptr, 0.0f, &isOpen,
                         icons);
    if (open != nullptr) *open = isOpen;
    st.opened = isOpen;
    return pick;
}

} // namespace argon::ui
