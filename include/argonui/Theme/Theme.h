// ArgonUI - Theme/Theme.h
//
// Every visual decision lives here. A widget never hard-codes a colour, a
// radius or a height; it reads them from the active Theme. That keeps the
// reference look reproducible as a data change rather than a code change, and
// makes per-window theming trivial.
#pragma once

#include "../Core/Color.h"
#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "../Text/Font.h"

namespace argon::ui {

/// Interaction-aware colour set. Widgets pick the entry matching their state.
struct StateColors {
    Color normal;
    Color hovered;
    Color active;
    Color disabled;

    Color For(bool hovered, bool active, bool disabled) const {
        if (disabled) return disabled;
        if (active) return active;
        if (hovered) return hovered;
        return normal;
    }
};

/// Per-visual-variant button styling.
struct ButtonVariant {
    Color bgNormal;
    Color bgHovered;
    Color bgActive;
    Color borderNormal;
    Color borderHovered;
    Color fgNormal;
    Color fgHovered;
    Color fgDisabled;
    f32 borderWidth = 0.0f;
    f32 radius = 8.0f;
    f32 height = 34.0f;
    Vec2 padding{14.0f, 0.0f};
    f32 fontSize = FontManager::kSizeBody;
    bool bold = false;
};

struct Palette {
    // Surfaces.
    Color windowBg{Color::Hex(0x0B0C0Eu), 0.98f};
    Color windowBorder{Color::Hex(0x23262Bu)};
    Color sidebarBg{Color::Hex(0x101216u)};
    Color titleBarBg{Color::Hex(0x0E1013u)};
    Color cardBg{Color::Hex(0x16181Cu)};
    Color cardBgHover{Color::Hex(0x1B1E23u)};
    Color cardBorder{Color::Hex(0x25282Eu)};
    Color panelBg{Color::Hex(0x111317u)};
    Color fieldBg{Color::Hex(0x1D2025u)};
    Color fieldBgHover{Color::Hex(0x23262Cu)};
    Color fieldBorder{Color::Hex(0x2B2F36u)};
    Color fieldBorderFocus{Color::Hex(0x3A3F48u)};
    Color separator{Color::Hex(0x22252Au)};
    Color sectionHeaderBg{Color::Hex(0x191C20u)};

    // Accent (the lime in the reference UI).
    Color accent{Color::Hex(0xA3E635u)};
    Color accentHover{Color::Hex(0xB4F04Du)};
    Color accentPressed{Color::Hex(0x8CC72Bu)};
    Color accentSoft{Color::Hex(0xA3E635u), 0.16f};
    Color onAccent{Color::Hex(0x10150Au)};

    // Text.
    Color text{Color::Hex(0xF2F4F7u)};
    Color textDim{Color::Hex(0x9AA1ACu)};
    Color textMuted{Color::Hex(0x656C77u)};
    Color textDisabled{Color::Hex(0x454B54u)};
    Color textOnAccentDark{Color::Hex(0x1B2400u)};
    Color danger{Color::Hex(0xF2555Au)};
    Color dangerSoft{Color::Hex(0xF2555Au), 0.16f};
    Color warning{Color::Hex(0xF5B94Cu)};
    Color success{Color::Hex(0x4ADE80u)};
    Color info{Color::Hex(0x60A5FAu)};

    // Controls.
    Color track{Color::Hex(0x33373Fu)};
    Color trackHover{Color::Hex(0x3D424Bu)};
    Color thumb{Color::Hex(0xF2F4F7u)};
    Color thumbAccent{Color::Hex(0xA3E635u)};
    Color sliderRail{Color::Hex(0x2A2E35u)};
    Color scrollbar{Color::Hex(0x4A5059u)};
    Color scrollbarHover{Color::Hex(0x656C77u)};
    Color selection{Color::Hex(0xA3E635u), 0.20f};

    // Effects.
    Color shadow{Color::Hex(0x000000u), 0.55f};
    Color glow{Color::Hex(0xA3E635u), 0.35f};
    Color backdrop{Color::Hex(0x000000u), 0.62f};
    Color white{Color(1, 1, 1, 1)};
    Color transparent{Color(0, 0, 0, 0)};
};

struct Metrics {
    // Window chrome.
    f32 windowRadius = 14.0f;
    f32 windowBorderWidth = 1.0f;
    f32 windowPadding = 12.0f;
    f32 windowShadowSize = 28.0f;

    f32 titleBarHeight = 52.0f;
    f32 sidebarWidth = 56.0f;
    f32 sidebarItemSize = 38.0f;
    f32 sidebarItemRadius = 10.0f;
    f32 sidebarItemGap = 4.0f;

    // Rows & sections.
    f32 rowHeight = 34.0f;
    f32 rowPaddingX = 14.0f;
    f32 rowGap = 0.0f;
    f32 sectionRadius = 12.0f;
    f32 sectionHeaderHeight = 40.0f;
    f32 sectionHeaderPaddingX = 14.0f;
    f32 sectionContentPadding = 8.0f;
    f32 separatorThickness = 1.0f;

    // Buttons.
    f32 buttonHeight = 34.0f;
    f32 buttonRadius = 9.0f;
    f32 buttonPaddingX = 14.0f;
    f32 buttonGap = 8.0f;
    f32 buttonIconGap = 8.0f;
    f32 smallButtonHeight = 28.0f;
    f32 iconButtonSize = 32.0f;

    // Fields.
    f32 fieldHeight = 32.0f;
    f32 fieldRadius = 9.0f;
    f32 fieldPaddingX = 11.0f;
    f32 checkboxRadius = 6.0f;
    f32 checkboxSize = 20.0f;

    // Toggle.
    f32 toggleWidth = 42.0f;
    f32 toggleHeight = 23.0f;
    f32 toggleRadius = 11.5f;
    f32 toggleKnobRadius = 8.5f;

    // Slider.
    f32 sliderHeight = 18.0f;   ///< Total hit area.
    f32 sliderRailThickness = 5.0f;
    f32 sliderThumbRadius = 8.0f;

    // Tabs.
    f32 tabHeight = 38.0f;
    f32 tabPaddingX = 16.0f;
    f32 tabRadius = 9.0f;
    f32 tabIndicatorThickness = 2.5f;

    // Popups / modals.
    f32 popupPadding = 6.0f;
    f32 popupRadius = 12.0f;
    f32 popupItemHeight = 30.0f;
    f32 popupItemPaddingX = 12.0f;
    f32 popupShadowSize = 20.0f;
    f32 modalRadius = 16.0f;
    f32 modalHeaderHeight = 56.0f;
    f32 modalFooterHeight = 64.0f;
    f32 modalPadding = 20.0f;
    f32 modalMinWidth = 320.0f;
    f32 modalMinHeight = 200.0f;

    // Grid / list.
    f32 gridCellRadius = 10.0f;
    f32 gridCellPadding = 8.0f;
    f32 listItemHeight = 38.0f;
    f32 listItemRadius = 9.0f;
    f32 listIconSize = 28.0f;

    // Misc.
    f32 scrollbarWidth = 5.0f;
    f32 scrollbarPadding = 2.0f;
    f32 gap = 8.0f;
    f32 gapSmall = 5.0f;
    f32 gapLarge = 16.0f;
    f32 separatorInset = 14.0f;
    f32 badgeHeight = 20.0f;
    f32 badgeRadius = 6.0f;
    f32 badgePaddingX = 8.0f;
};

/// Animation timings. Centralised so the whole UI feels like one product.
struct Motion {
    /// Rate constants for Damp(); higher = snappier.
    f32 hoverRate = 16.0f;
    f32 pressRate = 28.0f;
    f32 expandRate = 14.0f;
    f32 toggleRate = 22.0f;
    f32 modalInRate = 15.0f;
    f32 modalOutRate = 22.0f;
    f32 scrollRate = 30.0f;

    f32 modalInDuration = 0.16f;
    f32 modalOutDuration = 0.11f;
    f32 tooltipDelay = 0.45f;
    f32 tooltipFade = 0.10f;
    f32 contextMenuDuration = 0.12f;
};

struct Theme {
    Palette colors;
    Metrics metrics;
    Motion motion;

    ButtonVariant primaryButton{};
    ButtonVariant secondaryButton{};
    ButtonVariant ghostButton{};
    ButtonVariant dangerButton{};
    ButtonVariant iconButton{};

    static Theme Dark();
    static Theme Light();

    /// A softer variant used for secondary surfaces.
    static Theme Midnight();

    void InitVariants();
};

} // namespace argon::ui