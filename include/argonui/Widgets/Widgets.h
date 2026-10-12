// ArgonUI - Widgets/Widgets.h
// Containers, pickers and overlays - the composite widgets that make up an
// actual menu rather than a row of buttons.
#pragma once

#include "Controls.h"
#include "Icons.h"
#include "Internal.h"

namespace argon::ui {

// ===========================================================================
// Containers
// ===========================================================================

/// A collapsible card with a header bar, exactly like the reference sections.
/// Returns the new open state; pair with EndSection. Always draw the body -
/// when the section is closed it is clipped to nothing, so there is no
/// "forgot to check" flicker and no duplicate layout path.
bool BeginSection(Context& ctx, const char* title, bool* open = nullptr,
                  const char* subtitle = nullptr, Icon icon = Icon::None);
void EndSection(Context& ctx);

/// Scrolling region. Everything between Begin/End is clipped and offset.
/// Returns a std::function-free callback style: draw with the inner content
/// rect available via ScrollAreaContent(). Returns true if it scrolled this
/// frame (useful to swallow the wheel).
bool BeginScrollArea(Context& ctx, const char* id, f32 height, bool* scrollY = nullptr,
                     bool showScrollbar = true);
Rect ScrollAreaContent();
void EndScrollArea(Context& ctx);

/// Underlined tab bar, matching the "Visuals | Enemy | Local | World" strip.
/// Returns the new active index. `animatedOut` receives the animated underline
/// rect so the caller can render it.
int TabBar(Context& ctx, const char* id, const char* const* tabs, int count, int* active,
           Rect* animatedIndicator = nullptr, Icon* tabIcons = nullptr);

/// Icon rail sidebar. Returns the new selected index.
int Sidebar(Context& ctx, const char* id, const Icon* icons, int count, int* selected,
            f32 width = 0.0f, const int* badges = nullptr);

/// Window frame: background, hairline border, drop shadow, optional title bar
/// with a close button and draggable by its title bar.
bool BeginWindow(Context& ctx, const char* id, const char* title, const Vec2& size,
                 bool* open = nullptr, Icon appIcon = Icon::None);
void EndWindow(Context& ctx);

/// Same window, but positioned by the caller instead of auto-centred.
bool BeginWindowAt(Context& ctx, const char* id, const char* title, const Rect& bounds,
                    bool* open = nullptr, Icon appIcon = Icon::None);

/// Evenly spaced gallery grid. `drawCell(ctx, cellRect, index)` is called for
/// each cell; return false to skip it.
void Grid(Context& ctx, int columns, f32 cellSize, f32 spacing,
          bool (*drawCell)(Context&, const Rect&, int index, void* userData), int count,
          void* userData = nullptr);

/// Vertical list with optional icons, subtitles and selection.
struct ListItem {
    const char* label = nullptr;
    const char* detail = nullptr;    ///< Right-aligned secondary text.
    Icon icon = Icon::None;
    Color dotColor = Color(0, 0, 0, 0);
    u32 textureId = 0;               ///< Optional thumbnail.
    bool disabled = false;
};
int List(Context& ctx, const char* id, const ListItem* items, int count, int* selected,
         f32 rowHeight = 0.0f);

/// Label + control row helper. Declared in Controls.h; re-exported for
/// convenience so one include is enough for most menus.
bool Row(Context& ctx, const char* label, const RowOptions& options,
         bool (*drawControl)(Context&, const Rect&, void* userData), void* userData = nullptr);

/// Simple key/value row with no interactive control.
void InfoRow(Context& ctx, const char* label, const char* value, Color dotColor = Color(0, 0, 0, 0),
             Icon icon = Icon::None);

// ===========================================================================
// Pickers / inputs
// ===========================================================================

/// Single-line editable text. Returns true when the value changed.
bool TextField(Context& ctx, const char* id, char* buffer, size_t bufferSize,
               const char* placeholder = nullptr, bool enabled = true, Icon icon = Icon::None);

/// std::string convenience wrapper.
bool TextField(Context& ctx, const char* id, std::string& value,
               const char* placeholder = nullptr, bool enabled = true, Icon icon = Icon::None);

/// Numeric text field with drag-to-scrub on the label.
bool DragFloat(Context& ctx, const char* id, f32* value, f32 speed = 0.1f, f32 vmin = 0.0f,
               f32 vmax = 0.0f, const char* fmt = "%.2f");
bool DragInt(Context& ctx, const char* id, int* value, f32 speed = 1.0f, int vmin = 0, int vmax = 0);

/// Search field with a magnifier glyph, placeholder and a clear button.
/// Returns true when the query changed.
bool SearchField(Context& ctx, const char* id, std::string& query,
                 const char* placeholder = "Search", bool enabled = true);

/// Multi-line wrapped, read-only text block.
void TextBlock(Context& ctx, const char* text, const Color& color = Color(0, 0, 0, 0),
               f32 fontSize = 0.0f);

// ===========================================================================
// Overlays
// ===========================================================================

/// Modal dialog. Returns true when it is open this frame; the caller draws the
/// body between Begin/End. The backdrop dims everything and swallows input.
bool BeginModal(Context& ctx, const char* id, const char* title, const Vec2& size,
                bool* open = nullptr, Icon icon = Icon::None, const char* closeLabel = nullptr);
void EndModal(Context& ctx);

/// Content area of the currently open modal (between Begin/End).
Rect ModalContent();
/// Footer area of the currently open modal.
Rect ModalFooter();
/// Header area of the currently open modal.
Rect ModalHeader();

/// Right-click / "..." context menu. `items` are label + optional icon.
/// Returns the clicked index, or -1.
int ContextMenu(Context& ctx, const char* id, const char* const* labels, int count,
                const Icon* icons = nullptr, bool* open = nullptr);

/// Simple dropdown list used by Combo and ContextMenu internally, and
/// available on its own.
int PopupList(Context& ctx, const char* id, const Vec2& anchor, const char* const* labels,
              int count, int* selected, f32 width = 0.0f, bool* open = nullptr,
              const Icon* icons = nullptr);

/// Draws everything queued by ShowTooltip. Call once at the very end of a frame.
void RenderTooltips(Context& ctx);

} // namespace argon::ui