// ArgonUI - examples/demo/DemoMenu.cpp
//
// Builds the reference layout: window frame -> icon rail + title bar -> tab
// strip -> scrollable column of collapsible sections, plus a modal and a
// context menu to exercise the overlay path.
#include "DemoMenu.h"

#include <cstring>

using namespace argon::ui;

namespace argondemo {
namespace {
// Small "draw this into the right-hand slot of a Row" adapters.
// A Row hands the control a rect; these place a widget inside it and report
// whether it changed.

struct BoolRef {
    bool* v;
    bool changed = false;
};
bool DrawSwitch(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<BoolRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::ToggleHeight);
    const f32 w = ctx.Var(StyleVarId::ToggleWidth);
    ctx.SetCursorPos(Vec2(r.max.x - w, r.GetCenter().y - h * 0.5f));
    ref->changed = ToggleSwitch(ref->v);
    return ref->changed;
}

struct SwatchRef {
    Color* c;
};
bool DrawSwatch(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<SwatchRef*>(ud);
    const f32 s = Min(22.0f, r.Height() * 0.8f);
    const Rect box(r.max.x - s, r.GetCenter().y - s * 0.5f, r.max.x, r.GetCenter().y + s * 0.5f);
    const ItemResult ir = ctx.ItemBehavior(ctx.GetID("swatch"), box);
    ctx.AddBox(box, *ref->c, CornerRadius(6.0f), 1.5f,
               ctx.Color(StyleColorId::White).WithAlpha(0.35f));
    if (ir.hovered) ctx.AddBox(box, Color(1, 1, 1, 0.10f), CornerRadius(6.0f));
    return ir.clicked;
}

struct SliderRef {
    f32* v;
    f32 vmin;
    f32 vmax;
    const char* fmt;
    bool changed = false;
};
bool DrawSlider(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<SliderRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::SliderHeight);
    ctx.SetCursorPos(Vec2(r.min.x, r.GetCenter().y - h * 0.5f));
    ref->changed = SliderFloat("", ref->v, ref->vmin, ref->vmax, ref->fmt);
    return ref->changed;
}

struct DragRef {
    int* v;
    bool changed = false;
};
bool DrawDragInt(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<DragRef*>(ud);
    const f32 h = ctx.Var(StyleVarId::FrameHeight);
    ctx.SetCursorPos(Vec2(r.max.x - 140.0f, r.GetCenter().y - h * 0.5f));
    ref->changed = DragInt(ctx, "row.drag", ref->v, 1.0f, 0, 99999);
    return ref->changed;
}

struct CtxRef {
    MenuState* s;
};
bool DrawFooter(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<CtxRef*>(ud)->s;
    bool any = false;
    const f32 h = ctx.Var(StyleVarId::ButtonHeight);
    const Context::CursorState saved = ctx.SaveCursor();

    ctx.SetCursorPos(Vec2(r.min.x, r.GetCenter().y - h * 0.5f));
    any |= ButtonWithIcon("Save", Icon::Save, ButtonKind::Primary, IconSlot::Left,
                          Vec2(112.0f, h));
    ctx.SameLine();
    if (ButtonWithIcon("Default", Icon::FolderOpen, ButtonKind::Secondary, IconSlot::Left,
                       Vec2(132.0f, h))) {
        ref->tab = 0;
        any = true;
    }
    ctx.SameLine();
    if (ButtonWithIcon("Modal", Icon::Grid, ButtonKind::Ghost, IconSlot::Left,
                       Vec2(110.0f, h))) {
        ref->modalOpen = true;
        any = true;
    }
    ctx.SameLine();
    ctx.SetCursorPos(Vec2(r.max.x - 34.0f, r.GetCenter().y - h * 0.5f));
    if (IconButton(Icon::Sliders, "Toggle ArgonUI metrics")) {
        ref->showMetrics = !ref->showMetrics;
        any = true;
    }

    ctx.RestoreCursor(saved);
    return any;
}

// Static demo state that lives outside MenuState because the reference UI
// treats these as part of the "skin" being edited.
struct SkinState {
    f32 wear = 0.62f;
    f32 seed = 267.0f;
    bool stattrak = true;
    int kills = 4476;
    std::string nameTag = "123";
    Color primary = Color::Hex(0xF2292Bu);
    Color secondary = Color::Hex(0xF2F2F2u);
    Color accent = Color::Hex(0xA3E635u);
    int effect = 1;
    std::string search;
    bool sections[4] = {true, true, true, false};
};

// Tabs

const char* const kTabs[] = {"Visuals", "Enemy", "Local", "World", "Ambience"};
constexpr int kTabCount = 5;

void DrawVisualsTab(Context& ctx, MenuState& s, SkinState& k) {
    RowOptions o;

    // ---- Player -------------------------------------------------------
    BeginSection(ctx, "Player", &k.sections[0], nullptr, Icon::User);
    RowToggle(ctx, "Enabled", &s.espEnabled);

    {
        BoolRef ref{&s.outOfScreenArrows};
        o = RowOptions{};
        o.dotColor = Color::Hex(0xF472B6u);
        Row(ctx, "Out of screen arrows", o, DrawSwitch, &ref);
    }
    {
        BoolRef ref{&s.soundEsp};
        o = RowOptions{};
        o.icon = Icon::Bolt;
        Row(ctx, "Sound ESP", o, DrawSwitch, &ref);
    }
    {
        // Two-pill segmented control: Visible / Behind wall.
        static const char* opts[] = {"Visible", "Behind wall"};
        o = RowOptions{};
        o.icon = Icon::Eye;
        o.separatorAfter = true;
        struct SegRef { int* v; bool c = false; };
        SegRef ref{&s.entityMode};
        Row(ctx, "Draw mode", o,
            [](Context& c, const Rect& r, void* ud) -> bool {
                auto* st = static_cast<SegRef*>(ud);
                static const char* items[] = {"Visible", "Behind wall"};
                const f32 h = c.Var(StyleVarId::ButtonHeight) - 2.0f;
                const f32 w = Min(104.0f, r.Width() * 0.5f);
                c.SetCursorPos(Vec2(r.max.x - w * 2.0f, r.GetCenter().y - h * 0.5f));
                st->c = SegmentedControl("row.seg", items, 2, st->v, w) > 0;
                return st->c;
            },
            &ref);
        (void)opts;
    }
    EndSection(ctx);

    // ---- Model --------------------------------------------------------
    BeginSection(ctx, "Model", &k.sections[1], nullptr, Icon::Cube);
    RowToggle(ctx, "Chams", &s.wallChams, Icon::Layers);
    RowToggle(ctx, "Hit chams", &s.tracers, Icon::Target);
    RowToggle(ctx, "Ragdoll chams", &s.glow, Icon::Cube);

    {
        BoolRef ref{&s.glow};
        o = RowOptions{};
        o.icon = Icon::Sparkle;
        Row(ctx, "Glow", o, DrawSwitch, &ref);
    }
    {
        SliderRef ref{&s.glowStrength, 0.0f, 1.0f, "%.2f"};
        o = RowOptions{};
        o.icon = Icon::Sliders;
        o.overflowButton = true;
        Row(ctx, "Glow strength", o, DrawSlider, &ref);
    }
    EndSection(ctx);

    // ---- Renders ------------------------------------------------------
    BeginSection(ctx, "Renders", &k.sections[2]);
    {
        SliderRef ref{&s.fov, 60.0f, 130.0f, "%.0f"};
        o = RowOptions{};
        Row(ctx, "FOV", o, DrawSlider, &ref);
    }
    {
        SliderRef ref{&s.boxThickness, 0.5f, 5.0f, "%.1f"};
        o = RowOptions{};
        Row(ctx, "Box thickness", o, DrawSlider, &ref);
    }
    {
        SliderRef ref{&s.smoothness, 0.0f, 4.0f, "%.0f"};
        o = RowOptions{};
        Row(ctx, "Smoothing", o, DrawSlider, &ref);
    }
    EndSection(ctx);

    // ---- Collapsed-by-default section ---------------------------------
    BeginSection(ctx, "Advanced", &k.sections[3]);
    InfoRow(ctx, "Draw calls", "12");
    InfoRow(ctx, "Backend", "D3D11", Color::Hex(0x4ADE80u));
    EndSection(ctx);
}

void DrawSkinTab(Context& ctx, MenuState& s, SkinState& k) {
    RowOptions o;

    BeginSection(ctx, "Stickers & charm", &k.sections[0], nullptr, Icon::Sparkle);
    RowCombo(ctx, "Weapon", s.skins, 4, &s.skinIndex);

    {
        SliderRef ref{&k.wear, 0.0f, 1.0f, "%.2f"};
        Row(ctx, "Wear", o, DrawSlider, &ref);
    }
    {
        SliderRef ref{&k.seed, 0.0f, 1000.0f, "%.0f"};
        Row(ctx, "Pattern seed", o, DrawSlider, &ref);
    }
    {
        BoolRef ref{&k.stattrak};
        Row(ctx, "StatTrak", o, DrawSwitch, &ref);
    }
    {
        DragRef ref{&k.kills};
        Row(ctx, "Kills", o, DrawDragInt, &ref);
    }
    TextField(ctx, "skin.nametag", k.nameTag, "Name tag");
    EndSection(ctx);

    BeginSection(ctx, "Override colors", &k.sections[1]);
    {
        SwatchRef ref{&k.primary};
        o = RowOptions{};
        Row(ctx, "Primary", o, DrawSwatch, &ref);
    }
    {
        SwatchRef ref{&k.secondary};
        o = RowOptions{};
        o.separatorAfter = false;
        Row(ctx, "Secondary", o, DrawSwatch, &ref);
    }
    EndSection(ctx);

    BeginSection(ctx, "Equip", &k.sections[2]);
    RowCombo(ctx, "Team", s.teams, 3, &s.teamMode);
    {
        o = RowOptions{};
        o.separatorAfter = false;
        Row(ctx, "Equips for Counter-Terrorists and the other team", o,
            [](Context& c, const Rect& r, void*) -> bool {
                const f32 h = c.Var(StyleVarId::ButtonHeight);
                c.SetCursorPos(Vec2(r.max.x - 112.0f, r.GetCenter().y - h * 0.5f));
                return ButtonWithIcon("Equip", Icon::Check, ButtonKind::Primary,
                                      IconSlot::Left, Vec2(112.0f, h));
            },
            nullptr);
    }
    EndSection(ctx);
}

struct GalleryEntry {
    const char* name;
    Icon icon;
};

void DrawGallery(Context& ctx) {
    static const GalleryEntry entries[] = {
        {"Glow", Icon::Bolt},      {"Outline", Icon::Crosshair}, {"Chams", Icon::Layers},
        {"Radar", Icon::Target},   {"Blur", Icon::Cube},        {"Sparkle", Icon::Sparkle},
    };
    const int n = static_cast<int>(sizeof(entries) / sizeof(entries[0]));

    Grid(ctx, 3, 108.0f, 12.0f,
         [](Context& c, const Rect& r, int index, void* ud) -> bool {
             const auto* e = static_cast<const GalleryEntry*>(ud) + index;
             const f32 gs = 32.0f;
             const Rect ir(r.GetCenter().x - gs * 0.5f, r.min.y + 22.0f,
                           r.GetCenter().x + gs * 0.5f, r.min.y + 22.0f + gs);
             DrawIcon(c.MutableDrawList(), e->icon, ir, 2.2f, c.Color(StyleColorId::Accent));

             Font* f = c.FontAt(c.Var(StyleVarId::TextSizeSmall));
             const f32 tw = f->MeasureLine(e->name, std::strlen(e->name));
             c.AddText(Vec2(r.GetCenter().x - tw * 0.5f, r.max.y - 14.0f), e->name,
                       c.Color(StyleColorId::TextDim), f);
             return false;
         },
         n, const_cast<GalleryEntry*>(entries));
}

} // namespace

void DrawMenu(Context& ctx, MenuState& s) {
    static SkinState k;

    const Vec2 display = ctx.Frame().displaySize;
    const Vec2 size(Min(1080.0f, display.x - 60.0f), Min(680.0f, display.y - 60.0f));

    if (!BeginWindow(ctx, "argon.main", "Argon Crystal", size, nullptr, Icon::Sparkle)) {
        RenderTooltips(ctx);
        return;
    }

    // ---- Tab strip -----------------------------------------------------
    static const Icon kTabIcons[kTabCount] = {Icon::Eye,      Icon::Crosshair, Icon::User,
                                              Icon::Cube,     Icon::Palette};
    Rect indicator;
    TabBar(ctx, "tabs", kTabs, kTabCount, &s.tab, &indicator, kTabIcons);
    ctx.Spacing();

    // ---- Scrollable body ------------------------------------------------
    f32 scroll = 0.0f;
    BeginScrollArea(ctx, "body.scroll", 0.0f, &scroll, true);

    switch (s.tab) {
    case 1:
        DrawVisualsTab(ctx, s, k);
        ctx.Spacing();
        DrawGallery(ctx);
        break;
    case 2:
    case 3:
    case 4:
        DrawSkinTab(ctx, s, k);
        break;
    default:
        DrawVisualsTab(ctx, s, k);
        break;
    }

    ctx.Spacing();
    ctx.Separator();
    ctx.Spacing();

    {
        CtxRef ref{&s};
        RowOptions o;
        o.separatorAfter = false;
        Row(ctx, nullptr, o, DrawFooter, &ref);
    }

    EndScrollArea(ctx);

    // ---- Modal ----------------------------------------------------------
    if (BeginModal(ctx, "argon.modal", "Appearance", Vec2(560.0f, 360.0f), &s.modalOpen,
                   Icon::Palette, "Apply")) {
        ctx.BeginLayout(ModalContent());
        ctx.Spacing();
        TextField(ctx, "modal.search", k.search, "Search effects");
        ctx.Spacing();

        {
            static const char* opts[] = {"Glow", "Outline", "Chams", "Radar"};
            static Icon effIcons[4] = {Icon::Bolt, Icon::Crosshair, Icon::Layers, Icon::Target};
            (void)effIcons;
            SegmentedControl("modal.effect", opts, 4, &k.effect);
        }
        ctx.Spacing();
        {
            SwatchRef ref{&k.accent};
            RowOptions o;
            o.separatorAfter = false;
            Row(ctx, "Accent color", o, DrawSwatch, &ref);
        }
        ctx.Spacing();
        ctx.EndLayout();
        EndModal(ctx);
    }

    // ---- Context menu (right click anywhere) ----------------------------
    {
        static const char* items[] = {"Copy", "Paste", "Reset", "Delete"};
        static const Icon icons[4] = {Icon::Copy, Icon::Clipboard, Icon::Refresh, Icon::Trash};
        const int picked = ContextMenu(ctx, "ctx", items, 4, icons, &s.contextOpen);
        if (picked >= 0) s.contextOpen = false;
    }

    // ---- Tooltips float above everything ---------------------------------
    RenderTooltips(ctx);

    EndWindow(ctx);
}

} // namespace argondemo
