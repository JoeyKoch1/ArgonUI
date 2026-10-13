#include "argonui/Theme/Theme.h"

namespace argon::ui {
void Theme::InitVariants() {
    // Primary: solid accent, dark text. The "Save" / "Equip" buttons.
    primaryButton.bgNormal = colors.accent;
    primaryButton.bgHovered = colors.accentHover;
    primaryButton.bgActive = colors.accentPressed;
    primaryButton.borderWidth = 0.0f;
    primaryButton.fgNormal = colors.onAccent;
    primaryButton.fgHovered = colors.onAccent;
    primaryButton.fgDisabled = colors.accentSoft;
    primaryButton.radius = metrics.buttonRadius;
    primaryButton.height = metrics.buttonHeight;
    primaryButton.padding = Vec2(metrics.buttonPaddingX, 0.0f);

    // Secondary: raised surface with a hairline border. The "Default" buttons.
    secondaryButton.bgNormal = colors.fieldBg;
    secondaryButton.bgHovered = colors.fieldBgHover;
    secondaryButton.bgActive = colors.cardBgHover;
    secondaryButton.borderWidth = 1.0f;
    secondaryButton.fgNormal = colors.text;
    secondaryButton.fgHovered = colors.text;
    secondaryButton.fgDisabled = colors.textDisabled;
    secondaryButton.radius = metrics.buttonRadius;
    secondaryButton.height = metrics.buttonHeight;
    secondaryButton.padding = Vec2(metrics.buttonPaddingX, 0.0f);

    // Ghost: no surface until hovered. Inline actions, close buttons.
    ghostButton.bgNormal = colors.transparent;
    ghostButton.bgHovered = colors.cardBgHover;
    ghostButton.bgActive = colors.cardBg;
    ghostButton.borderWidth = 0.0f;
    ghostButton.fgNormal = colors.textDim;
    ghostButton.fgHovered = colors.text;
    ghostButton.fgDisabled = colors.textDisabled;
    ghostButton.radius = metrics.buttonRadius;
    ghostButton.height = metrics.smallButtonHeight;
    ghostButton.padding = Vec2(10.0f, 0.0f);

    // Danger: destructive confirmations.
    dangerButton.bgNormal = colors.danger;
    dangerButton.bgHovered = Color::LerpC(colors.danger, colors.white, 0.12f);
    dangerButton.bgActive = Color::LerpC(colors.danger, colors.text, 0.20f);
    dangerButton.borderWidth = 0.0f;
    dangerButton.fgNormal = colors.white;
    dangerButton.fgHovered = colors.white;
    dangerButton.fgDisabled = colors.dangerSoft;
    dangerButton.radius = metrics.buttonRadius;
    dangerButton.height = metrics.buttonHeight;
    dangerButton.padding = Vec2(metrics.buttonPaddingX, 0.0f);

    // Icon: square-ish, no padding, icon only.
    iconButton.bgNormal = colors.transparent;
    iconButton.bgHovered = colors.cardBgHover;
    iconButton.bgActive = colors.cardBg;
    iconButton.borderWidth = 0.0f;
    iconButton.fgNormal = colors.textDim;
    iconButton.fgHovered = colors.text;
    iconButton.fgDisabled = colors.textDisabled;
    iconButton.radius = metrics.buttonRadius;
    iconButton.height = metrics.iconButtonSize;
    iconButton.padding = Vec2(0.0f, 0.0f);
}

Theme Theme::Dark() {
    Theme t;
    t.InitVariants();
    return t;
}

Theme Theme::Midnight() {
    Theme t = Dark();
    t.colors.windowBg = Color::Hex(0x07080Au, 0.97f);
    t.colors.sidebarBg = Color::Hex(0x0B0D10u);
    t.colors.titleBarBg = Color::Hex(0x0A0C0Fu);
    t.colors.cardBg = Color::Hex(0x131619u);
    t.metrics.windowShadowSize = 34.0f;
    t.InitVariants();
    return t;
}

Theme Theme::Light() {
    Theme t;
    Palette& c = t.colors;

    c.windowBg = Color::Hex(0xF7F8FAu, 0.99f);
    c.windowBorder = Color::Hex(0xDFE3E8u);
    c.sidebarBg = Color::Hex(0xFFFFFFu);
    c.titleBarBg = Color::Hex(0xFFFFFFu);
    c.cardBg = Color::Hex(0xFFFFFFu);
    c.cardBgHover = Color::Hex(0xF1F3F6u);
    c.cardBorder = Color::Hex(0xE4E7ECu);
    c.panelBg = Color::Hex(0xFFFFFFu);
    c.fieldBg = Color::Hex(0xF2F4F7u);
    c.fieldBgHover = Color::Hex(0xEAECF0u);
    c.fieldBorder = Color::Hex(0xDDE1E7u);
    c.fieldBorderFocus = Color::Hex(0xBFC5CEu);
    c.separator = Color::Hex(0xE6E9EDu);
    c.sectionHeaderBg = Color::Hex(0xF4F5F7u);

    c.accent = Color::Hex(0x5B8C00u);
    c.accentHover = Color::Hex(0x4E7A00u);
    c.accentPressed = Color::Hex(0x436B00u);
    c.accentSoft = Color::Hex(0x5B8C00u, 0.14f);
    c.onAccent = Color::Hex(0xFFFFFFu);

    c.text = Color::Hex(0x14181Du);
    c.textDim = Color::Hex(0x545B65u);
    c.textMuted = Color::Hex(0x848C97u);
    c.textDisabled = Color::Hex(0xAEB5BFu);
    c.textOnAccentDark = Color::Hex(0xFFFFFFu);

    c.danger = Color::Hex(0xD92D2Du);
    c.dangerSoft = Color::Hex(0xD92D2Du, 0.14f);
    c.warning = Color::Hex(0xB54708u);
    c.success = Color::Hex(0x067647u);
    c.info = Color::Hex(0x175CD3u);

    c.track = Color::Hex(0xD5D9DFu);
    c.trackHover = Color::Hex(0xC6CBD3u);
    c.thumb = Color::Hex(0xFFFFFFu);
    c.thumbAccent = Color::Hex(0x5B8C00u);
    c.sliderRail = Color::Hex(0xDFE3E8u);
    c.scrollbar = Color::Hex(0xC2C8D0u);
    c.scrollbarHover = Color::Hex(0x9AA2ACu);
    c.selection = Color::Hex(0x5B8C00u, 0.16f);

    c.shadow = Color::Hex(0x1B1F26u, 0.16f);
    c.glow = Color::Hex(0x5B8C00u, 0.30f);
    c.backdrop = Color::Hex(0x1B1F26u, 0.38f);

    t.metrics.windowShadowSize = 22.0f;
    t.InitVariants();
    return t;
}

} // namespace argon::ui
