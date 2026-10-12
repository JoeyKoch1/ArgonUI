// ArgonUI - Theme/Style.h
//
// The live style stack. Theme holds the defaults; Style holds a *mutable copy*
// with a push/pop stack on top, so widgets can be themed per-window, per-section
// or for the duration of a single call without leaking the change.
#pragma once

#include <vector>

#include "../Core/Color.h"
#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "Theme.h"

namespace argon::ui {
enum class StyleColorId : int {
    Text = 0,
    TextDim,
    TextMuted,
    TextDisabled,

    WindowBg,
    WindowBorder,
    SidebarBg,
    TitleBarBg,
    PanelBg,
    CardBg,
    CardBgHover,
    CardBorder,
    SectionHeaderBg,

    FieldBg,
    FieldBgHover,
    FieldBorder,
    FieldBorderFocus,

    Accent,
    AccentHover,
    AccentPressed,
    AccentSoft,
    OnAccent,

    Danger,
    DangerSoft,
    Warning,
    Success,
    Info,

    Track,
    TrackHover,
    Thumb,
    ThumbAccent,
    SliderRail,
    Scrollbar,
    ScrollbarHover,
    Selection,

    Separator,
    Shadow,
    Glow,
    Backdrop,
    White,
    Transparent,

    COUNT
};

enum class StyleVarId : int {
    Alpha = 0,
    DisabledAlpha,

    WindowRadius,
    WindowBorderWidth,
    WindowPadding,
    WindowShadowSize,

    FrameHeight,
    FrameRadius,
    FrameBorderWidth,
    FramePaddingX,

    ItemSpacingX,
    ItemSpacingY,
    ItemInnerSpacingX,
    ItemInnerSpacingY,
    CellPaddingX,
    CellPaddingY,

    TitleBarHeight,
    SidebarWidth,
    SidebarItemSize,
    SidebarItemRadius,
    SidebarItemGap,

    ButtonHeight,
    ButtonRadius,
    ButtonBorderWidth,
    ButtonPaddingX,
    SmallButtonHeight,
    IconButtonSize,

    CheckboxSize,
    CheckboxRadius,

    ToggleWidth,
    ToggleHeight,
    ToggleRadius,
    ToggleKnobRadius,

    SliderHeight,
    SliderRailThickness,
    SliderThumbRadius,

    TabHeight,
    TabPaddingX,
    TabRadius,
    TabIndicatorThickness,

    RowHeight,
    RowPaddingX,
    SeparatorThickness,
    SeparatorInset,

    SectionRadius,
    SectionHeaderHeight,
    SectionHeaderPaddingX,
    SectionContentPadding,

    PopupPadding,
    PopupRadius,
    PopupItemHeight,
    PopupItemPaddingX,
    PopupShadowSize,

    ModalRadius,
    ModalHeaderHeight,
    ModalFooterHeight,
    ModalPadding,
    ModalMinWidth,
    ModalMinHeight,

    ListItemHeight,
    ListItemRadius,
    ListIconSize,

    GridCellRadius,
    GridCellPadding,

    BadgeHeight,
    BadgeRadius,
    BadgePaddingX,

    Gap,
    GapSmall,
    GapLarge,
    GapLargeX,

    HoverRate,
    PressRate,
    ExpandRate,
    ToggleRate,
    ModalInRate,
    ModalOutRate,
    ScrollRate,

    TextSize,
    TextSizeSmall,
    TextSizeTiny,
    TextSizeTitle,
    TextSizeDisplay,

    COUNT
};

/// Read/write access to the current style values.
class Style {
public:
    Style();
    explicit Style(const Theme& theme);

    /// Re-seeds every value from `theme`, discarding the stack.
    void Reset(const Theme& theme);

    Color GetColor(StyleColorId id) const { return colors_[static_cast<int>(id)]; }
    f32 GetVar(StyleVarId id) const { return vars_[static_cast<int>(id)]; }

    void SetColor(StyleColorId id, const Color& c);
    void SetVar(StyleVarId id, f32 v);

    void PushColor(StyleColorId id, const Color& c);
    void PushVar(StyleVarId id, f32 v);
    void PopColors(int count = 1);
    void PopVars(int count = 1);

    int ColorDepth() const { return static_cast<int>(colorStack_.size()); }
    int VarDepth() const { return static_cast<int>(varStack_.size()); }
    void ClearStack();

    const Theme& Theme() const { return theme_; }
    Theme& MutableTheme() { return theme_; }

    f32 Alpha() const { return GetVar(StyleVarId::Alpha); }

private:
    struct ColorEntry { StyleColorId id; Color previous; };
    struct VarEntry { StyleVarId id; f32 previous; };

    Theme theme_;
    Color colors_[static_cast<int>(StyleColorId::COUNT)];
    f32 vars_[static_cast<int>(StyleVarId::COUNT)];
    std::vector<ColorEntry> colorStack_;
    std::vector<VarEntry> varStack_;
};

const char* StyleColorName(StyleColorId id);

} // namespace argon::ui
