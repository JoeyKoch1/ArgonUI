// ArgonUI - Widgets/Controls.h
//
// The interactive controls. Every one of them:
//   * takes its colours and metrics from the Style, never from a literal;
//   * persists what it needs (toggle position, scroll, selection) in the
//     Context's state store, keyed by its id - callers keep nothing;
//   * animates hover/press/expand through the shared helpers, so the whole UI
//     moves at the same rate;
//   * returns its own layout size and advances the cursor, so stacking widgets
//     reads as a list instead of coordinate arithmetic.
#pragma once

#include "../Core/Rect.h"
#include "Internal.h"
#include "Icons.h"

namespace argon::ui {
enum class ButtonKind {
    Primary = 0,   ///< Solid accent, dark text. "Save", "Equip".
    Secondary,     ///< Raised surface + hairline border. "Default".
    Ghost,         ///< No surface until hovered. Inline actions, close.
    Danger,        ///< Destructive confirmation.
    Warning,
};

enum class IconSlot {
    None = 0,
    Left,
    Right,
};

/// Theme colours for one button variant, resolved from the active theme.
struct ButtonColors {
    Color bg, bgHovered, bgActive, border, borderHovered, fg, fgDisabled;
    f32 borderWidth = 0.0f;
    f32 radius = 0.0f;
    f32 height = 0.0f;
    f32 paddingX = 0.0f;
};

ButtonColors ResolveButton(ButtonKind kind, const Style& style);

/// Standard text button. Returns true on click.
bool Button(const char* label, ButtonKind kind = ButtonKind::Secondary,
            const Vec2& size = Vec2(0.0f, 0.0f), bool enabled = true);

/// Text button with an icon in `slot`.
bool ButtonWithIcon(const char* label, Icon icon, ButtonKind kind = ButtonKind::Secondary,
                    IconSlot slot = IconSlot::Left, const Vec2& size = Vec2(0.0f, 0.0f),
                    bool enabled = true);

/// Square icon-only button. `tooltip` is shown after a short hover delay.
bool IconButton(Icon icon, const char* tooltip = nullptr, ButtonKind kind = ButtonKind::Ghost,
                f32 size = 0.0f, bool enabled = true, const Color* tint = nullptr);

/// Square checkbox with an animated tick.
bool Checkbox(const char* label, bool* value, bool enabled = true);

/// Pill switch. `label` may be null to draw the bare switch, which is how the
/// reference UI does it inside a Row.
bool ToggleSwitch(bool* value, bool enabled = true, f32 width = 0.0f, f32 height = 0.0f);

/// Horizontal slider. Click anywhere on the rail to jump, drag to scrub.
bool SliderFloat(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt = "%.2f",
                 bool enabled = true);

/// Vertical slider, for the rare panel that wants one.
bool SliderFloatVertical(const char* label, f32* value, f32 vmin, f32 vmax,
                         const char* fmt = "%.2f", f32 height = 120.0f, bool enabled = true);

/// Slider with an integer payload and an editable numeric field.
bool SliderInt(const char* label, int* value, int vmin, int vmax, const char* suffix = nullptr,
               bool enabled = true);

/// Determinate progress bar with an optional inline label.
void ProgressBar(f32 fraction, const char* label = nullptr, const Color& color = Color(0, 0, 0, 0));

/// Small tinted pill used for counts, statuses and tags.
void Badge(const char* text, const Color& color = Color(0, 0, 0, 0));
Vec2 BadgeSize(const char* text, const Color& color = Color(0, 0, 0, 0));

/// Segmented control: a rounded track with a sliding selected pill.
/// `id` must be unique among calls; it is not drawn. Returns the new selection.
int SegmentedControl(const char* id, const char* const* options, int count, int* selected,
                     f32 itemWidth = 0.0f);

/// Dropdown. `id` must be unique among calls; it is not drawn.
/// Returns the new selection, or -1 when dismissed.
int Combo(const char* id, const char* const* options, int count, int* selected, f32 width = 0.0f,
          const char* placeholder = nullptr);

/// Path-style breadcrumbs: "C: > Users > puzo > Downloads".
void Breadcrumbs(const char* const* items, int count, int* hoveredIndex = nullptr);

// Tooltip support (implemented in Overlays.cpp)

/// Queues a tooltip anchored to `anchor` for this frame. Safe to call from any
/// widget; the tooltip itself is drawn last so it floats above everything.
void ShowTooltip(const char* text, const Vec2& anchor);
void ShowTooltip(const char* text, const Rect& anchor);
/// Clears the queue. Called by Context::NewFrame.
void ClearTooltips();

// Row - the workhorse of the reference UI.
// A label on the left, a control on the right, hairline separators between
// rows, and an optional "..." overflow button. Widgets are passed as a callable
// so the control keeps full control of its own interaction.

struct RowOptions {
    /// Optional "..." button between label and control.
    bool overflowButton = false;
    /// Draw the hairline under this row.
    bool separatorAfter = true;
    /// Tint the whole row on hover.
    bool highlightOnHover = true;
    /// Draw an icon before the label.
    Icon icon = Icon::None;
    /// Draw a coloured dot before the label.
    Color dotColor = Color(0, 0, 0, 0);
    f32 height = 0.0f;        ///< 0 = style default.
    f32 minControlWidth = 0.0f;
    f32 labelFontSize = 0.0f;
};

/// Calls `drawControl(ctx, controlRect)` and returns true if it changed.
bool Row(Context& ctx, const char* label, const RowOptions& options,
         bool (*drawControl)(Context&, const Rect&, void* userData), void* userData = nullptr);

/// Convenience wrappers that place the most common controls in a Row. Each one
/// reaches for the current Context via Context::Get(), so a menu body can be
/// written without threading `ctx` through every line.
///
/// `icon` sits ahead of `enabled` so the common call is
/// `RowToggle("Chams", &on, Icon::Layers)` rather than one that spells out a
/// `true` first.
bool RowToggle(const char* label, bool* value, Icon icon = Icon::None, bool enabled = true);
bool RowSlider(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt = "%.2f",
               Icon icon = Icon::None, bool enabled = true);
bool RowCombo(const char* label, const char* const* options, int count, int* selected,
              Icon icon = Icon::None);
bool RowButton(const char* label, const char* buttonLabel, ButtonKind kind = ButtonKind::Secondary,
               Icon icon = Icon::None, IconSlot slot = IconSlot::Left);

} // namespace argon::ui
