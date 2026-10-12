#include "argonui/Theme/Style.h"

#include <cstring>

namespace argon::ui {
Style::Style() { Reset(Theme::Dark()); }
Style::Style(const Theme& theme) { Reset(theme); }

void Style::Reset(const Theme& theme) {
    theme_ = theme;
    colorStack_.clear();
    varStack_.clear();

    // Start every colour at fully transparent; SetColor below fills them.
    std::memset(colors_, 0, sizeof(colors_));
    std::memset(vars_, 0, sizeof(vars_));

    const Palette& c = theme_.colors;
    const Metrics& m = theme_.metrics;
    const Motion& mo = theme_.motion;

    auto col = [&](StyleColorId id, const Color& v) { colors_[static_cast<int>(id)] = v; };

    col(StyleColorId::Text, c.text);
    col(StyleColorId::TextDim, c.textDim);
    col(StyleColorId::TextMuted, c.textMuted);
    col(StyleColorId::TextDisabled, c.textDisabled);

    col(StyleColorId::WindowBg, c.windowBg);
    col(StyleColorId::WindowBorder, c.windowBorder);
    col(StyleColorId::SidebarBg, c.sidebarBg);
    col(StyleColorId::TitleBarBg, c.titleBarBg);
    col(StyleColorId::PanelBg, c.panelBg);
    col(StyleColorId::CardBg, c.cardBg);
    col(StyleColorId::CardBgHover, c.cardBgHover);
    col(StyleColorId::CardBorder, c.cardBorder);
    col(StyleColorId::SectionHeaderBg, c.sectionHeaderBg);

    col(StyleColorId::FieldBg, c.fieldBg);
    col(StyleColorId::FieldBgHover, c.fieldBgHover);
    col(StyleColorId::FieldBorder, c.fieldBorder);
    col(StyleColorId::FieldBorderFocus, c.fieldBorderFocus);

    col(StyleColorId::Accent, c.accent);
    col(StyleColorId::AccentHover, c.accentHover);
    col(StyleColorId::AccentPressed, c.accentPressed);
    col(StyleColorId::AccentSoft, c.accentSoft);
    col(StyleColorId::OnAccent, c.onAccent);

    col(StyleColorId::Danger, c.danger);
    col(StyleColorId::DangerSoft, c.dangerSoft);
    col(StyleColorId::Warning, c.warning);
    col(StyleColorId::Success, c.success);
    col(StyleColorId::Info, c.info);

    col(StyleColorId::Track, c.track);
    col(StyleColorId::TrackHover, c.trackHover);
    col(StyleColorId::Thumb, c.thumb);
    col(StyleColorId::ThumbAccent, c.thumbAccent);
    col(StyleColorId::SliderRail, c.sliderRail);
    col(StyleColorId::Scrollbar, c.scrollbar);
    col(StyleColorId::ScrollbarHover, c.scrollbarHover);
    col(StyleColorId::Selection, c.selection);

    col(StyleColorId::Separator, c.separator);
    col(StyleColorId::Shadow, c.shadow);
    col(StyleColorId::Glow, c.glow);
    col(StyleColorId::Backdrop, c.backdrop);
    col(StyleColorId::White, c.white);
    col(StyleColorId::Transparent, c.transparent);

    auto var = [&](StyleVarId id, f32 v) { vars_[static_cast<int>(id)] = v; };

    var(StyleVarId::Alpha, 1.0f);
    var(StyleVarId::DisabledAlpha, 0.45f);

    var(StyleVarId::WindowRadius, m.windowRadius);
    var(StyleVarId::WindowBorderWidth, m.windowBorderWidth);
    var(StyleVarId::WindowPadding, m.windowPadding);
    var(StyleVarId::WindowShadowSize, m.windowShadowSize);

    var(StyleVarId::FrameHeight, m.fieldHeight);
    var(StyleVarId::FrameRadius, m.fieldRadius);
    var(StyleVarId::FrameBorderWidth, 1.0f);
    var(StyleVarId::FramePaddingX, m.fieldPaddingX);

    var(StyleVarId::ItemSpacingX, m.gap);
    var(StyleVarId::ItemSpacingY, m.gapSmall);
    var(StyleVarId::ItemInnerSpacingX, m.gap);
    var(StyleVarId::ItemInnerSpacingY, m.gapSmall);
    var(StyleVarId::CellPaddingX, m.gridCellPadding);
    var(StyleVarId::CellPaddingY, m.gridCellPadding);

    var(StyleVarId::TitleBarHeight, m.titleBarHeight);
    var(StyleVarId::SidebarWidth, m.sidebarWidth);
    var(StyleVarId::SidebarItemSize, m.sidebarItemSize);
    var(StyleVarId::SidebarItemRadius, m.sidebarItemRadius);
    var(StyleVarId::SidebarItemGap, m.sidebarItemGap);

    var(StyleVarId::ButtonHeight, m.buttonHeight);
    var(StyleVarId::ButtonRadius, m.buttonRadius);
    var(StyleVarId::ButtonBorderWidth, 1.0f);
    var(StyleVarId::ButtonPaddingX, m.buttonPaddingX);
    var(StyleVarId::SmallButtonHeight, m.smallButtonHeight);
    var(StyleVarId::IconButtonSize, m.iconButtonSize);

    var(StyleVarId::CheckboxSize, m.checkboxSize);
    var(StyleVarId::CheckboxRadius, m.checkboxRadius);

    var(StyleVarId::ToggleWidth, m.toggleWidth);
    var(StyleVarId::ToggleHeight, m.toggleHeight);
    var(StyleVarId::ToggleRadius, m.toggleRadius);
    var(StyleVarId::ToggleKnobRadius, m.toggleKnobRadius);

    var(StyleVarId::SliderHeight, m.sliderHeight);
    var(StyleVarId::SliderRailThickness, m.sliderRailThickness);
    var(StyleVarId::SliderThumbRadius, m.sliderThumbRadius);

    var(StyleVarId::TabHeight, m.tabHeight);
    var(StyleVarId::TabPaddingX, m.tabPaddingX);
    var(StyleVarId::TabRadius, m.tabRadius);
    var(StyleVarId::TabIndicatorThickness, m.tabIndicatorThickness);

    var(StyleVarId::RowHeight, m.rowHeight);
    var(StyleVarId::RowPaddingX, m.rowPaddingX);
    var(StyleVarId::SeparatorThickness, m.separatorThickness);
    var(StyleVarId::SeparatorInset, m.separatorInset);

    var(StyleVarId::SectionRadius, m.sectionRadius);
    var(StyleVarId::SectionHeaderHeight, m.sectionHeaderHeight);
    var(StyleVarId::SectionHeaderPaddingX, m.sectionHeaderPaddingX);
    var(StyleVarId::SectionContentPadding, m.sectionContentPadding);

    var(StyleVarId::PopupPadding, m.popupPadding);
    var(StyleVarId::PopupRadius, m.popupRadius);
    var(StyleVarId::PopupItemHeight, m.popupItemHeight);
    var(StyleVarId::PopupItemPaddingX, m.popupItemPaddingX);
    var(StyleVarId::PopupShadowSize, m.popupShadowSize);

    var(StyleVarId::ModalRadius, m.modalRadius);
    var(StyleVarId::ModalHeaderHeight, m.modalHeaderHeight);
    var(StyleVarId::ModalFooterHeight, m.modalFooterHeight);
    var(StyleVarId::ModalPadding, m.modalPadding);
    var(StyleVarId::ModalMinWidth, m.modalMinWidth);
    var(StyleVarId::ModalMinHeight, m.modalMinHeight);

    var(StyleVarId::ListItemHeight, m.listItemHeight);
    var(StyleVarId::ListItemRadius, m.listItemRadius);
    var(StyleVarId::ListIconSize, m.listIconSize);

    var(StyleVarId::GridCellRadius, m.gridCellRadius);
    var(StyleVarId::GridCellPadding, m.gridCellPadding);

    var(StyleVarId::BadgeHeight, m.badgeHeight);
    var(StyleVarId::BadgeRadius, m.badgeRadius);
    var(StyleVarId::BadgePaddingX, m.badgePaddingX);

    var(StyleVarId::Gap, m.gap);
    var(StyleVarId::GapSmall, m.gapSmall);
    var(StyleVarId::GapLarge, m.gapLarge);
    var(StyleVarId::GapLargeX, m.gapLarge);

    var(StyleVarId::HoverRate, mo.hoverRate);
    var(StyleVarId::PressRate, mo.pressRate);
    var(StyleVarId::ExpandRate, mo.expandRate);
    var(StyleVarId::ToggleRate, mo.toggleRate);
    var(StyleVarId::ModalInRate, mo.modalInRate);
    var(StyleVarId::ModalOutRate, mo.modalOutRate);
    var(StyleVarId::ScrollRate, mo.scrollRate);

    var(StyleVarId::TextSize, FontManager::kSizeBody);
    var(StyleVarId::TextSizeSmall, FontManager::kSizeSmall);
    var(StyleVarId::TextSizeTiny, FontManager::kSizeTiny);
    var(StyleVarId::TextSizeTitle, FontManager::kSizeTitle);
    var(StyleVarId::TextSizeDisplay, FontManager::kSizeDisplay);
}

Color Style::GetColor(StyleColorId id) const { return colors_[static_cast<int>(id)]; }
f32 Style::GetVar(StyleVarId id) const { return vars_[static_cast<int>(id)]; }

void Style::SetColor(StyleColorId id, const Color& c) { colors_[static_cast<int>(id)] = c; }
void Style::SetVar(StyleVarId id, f32 v) { vars_[static_cast<int>(id)] = v; }

void Style::PushColor(StyleColorId id, const Color& c) {
    colorStack_.push_back({id, colors_[static_cast<int>(id)]});
    colors_[static_cast<int>(id)] = c;
}

void Style::PushVar(StyleVarId id, f32 v) {
    varStack_.push_back({id, vars_[static_cast<int>(id)]});
    vars_[static_cast<int>(id)] = v;
}

void Style::PopColors(int count) {
    for (int i = 0; i < count && !colorStack_.empty(); ++i) {
        const ColorEntry e = colorStack_.back();
        colorStack_.pop_back();
        colors_[static_cast<int>(e.id)] = e.previous;
    }
}

void Style::PopVars(int count) {
    for (int i = 0; i < count && !varStack_.empty(); ++i) {
        const VarEntry e = varStack_.back();
        varStack_.pop_back();
        vars_[static_cast<int>(e.id)] = e.previous;
    }
}

void Style::ClearStack() {
    colorStack_.clear();
    varStack_.clear();
}

const char* StyleColorName(StyleColorId id) {
    switch (id) {
    case StyleColorId::Text: return "Text";
    case StyleColorId::TextDim: return "TextDim";
    case StyleColorId::TextMuted: return "TextMuted";
    case StyleColorId::TextDisabled: return "TextDisabled";
    case StyleColorId::WindowBg: return "WindowBg";
    case StyleColorId::WindowBorder: return "WindowBorder";
    case StyleColorId::SidebarBg: return "SidebarBg";
    case StyleColorId::TitleBarBg: return "TitleBarBg";
    case StyleColorId::PanelBg: return "PanelBg";
    case StyleColorId::CardBg: return "CardBg";
    case StyleColorId::CardBgHover: return "CardBgHover";
    case StyleColorId::CardBorder: return "CardBorder";
    case StyleColorId::SectionHeaderBg: return "SectionHeaderBg";
    case StyleColorId::FieldBg: return "FieldBg";
    case StyleColorId::FieldBgHover: return "FieldBgHover";
    case StyleColorId::FieldBorder: return "FieldBorder";
    case StyleColorId::FieldBorderFocus: return "FieldBorderFocus";
    case StyleColorId::Accent: return "Accent";
    case StyleColorId::AccentHover: return "AccentHover";
    case StyleColorId::AccentPressed: return "AccentPressed";
    case StyleColorId::AccentSoft: return "AccentSoft";
    case StyleColorId::OnAccent: return "OnAccent";
    case StyleColorId::Danger: return "Danger";
    case StyleColorId::DangerSoft: return "DangerSoft";
    case StyleColorId::Warning: return "Warning";
    case StyleColorId::Success: return "Success";
    case StyleColorId::Info: return "Info";
    case StyleColorId::Track: return "Track";
    case StyleColorId::TrackHover: return "TrackHover";
    case StyleColorId::Thumb: return "Thumb";
    case StyleColorId::ThumbAccent: return "ThumbAccent";
    case StyleColorId::SliderRail: return "SliderRail";
    case StyleColorId::Scrollbar: return "Scrollbar";
    case StyleColorId::ScrollbarHover: return "ScrollbarHover";
    case StyleColorId::Selection: return "Selection";
    case StyleColorId::Separator: return "Separator";
    case StyleColorId::Shadow: return "Shadow";
    case StyleColorId::Glow: return "Glow";
    case StyleColorId::Backdrop: return "Backdrop";
    case StyleColorId::White: return "White";
    case StyleColorId::Transparent: return "Transparent";
    default: return "?";
    }
}

} // namespace argon::ui
