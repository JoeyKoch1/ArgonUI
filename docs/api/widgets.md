# Containers, pickers and overlays

The composite widgets that turn a row of buttons into an actual menu: window
frames, collapsible cards, scrolling regions, tab strips, sidebars, grids and
lists, the text and numeric inputs, and the overlay layer of modals, popups and
tooltips.

Header: [`include/argonui/Widgets/Widgets.h`](../../include/argonui/Widgets/Widgets.h).

## Pairing rules

Every `Begin` in this header pushes onto a stack and every `End` pops it. They
are not independently safe:

- Call the `End` exactly when the matching `Begin` returned true.
  `BeginWindow`, `BeginWindowAt` and `BeginModal` all return false, and for
  `BeginModal` one of those paths happens after the modal has already pushed its
  z layer and its input lock, so skipping the `End` there leaks both.
- `BeginSection` and `BeginScrollArea` are the exception: their regions are
  always open. `BeginSection` returns the open state and `BeginScrollArea`
  returns whether the area scrolled this frame, and neither is a test for
  whether the region is up. Call both `End` functions unconditionally.
- `Begin` and `End` of the same kind nest. A section inside a scroll area inside
  a window is a normal arrangement, not a special case.
- Widget state lives in the `Context` state store keyed by the widget's hashed
  id. Callers keep plain `bool`, `float` and `int`.

## Windows

### `BeginWindow`

```cpp
bool BeginWindow(Context& ctx, const char* id, const char* title, const Vec2& size,
                 bool* open = nullptr, Icon appIcon = Icon::None);
```

Draws a window frame centered on the display: drop shadow, body, hairline border
and an optional title bar. The content region below the title bar becomes the
active layout, so children go straight after the call with no positioning. A
window always owns hover over whatever was beneath it.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source. Unique among calls in the same scope. |
| `title` | `const char*` | | Title bar text, drawn in the semibold title font. |
| `size` | `const Vec2&` | | Frame size in pixels. Used as-is, so it must fit the display. |
| `open` | `bool*` | `nullptr` | When non-null, a close button appears in the title bar and this flag is cleared when it is clicked. While the flag is false the call returns false immediately. |
| `appIcon` | `Icon` | `Icon::None` | Glyph in a rounded accent tile at the left of the title bar. |

**Returns:** true when the window is open this frame. It returns false only
because `open` was false on entry, and in that case it has not pushed anything.

{% hint style="warning" %}
The title bar does not drag the window in this version. Size and position come
from `size`, or from `BeginWindowAt` if you need to place it yourself.
{% endhint %}

**Example**

```cpp
using namespace argon::ui;

const Vec2 display = ctx.Frame().displaySize;
const Vec2 size(Min(1080.0f, display.x - 60.0f), Min(680.0f, display.y - 60.0f));

if (BeginWindow(ctx, "argon.main", "Argon Crystal", size, &state.windowOpen, Icon::Sparkle)) {
    BeginSection(ctx, "Player", &state.playerOpen, nullptr, Icon::User);
        RowToggle(ctx, "Enabled", &state.espEnabled);
        RowSlider(ctx, "FOV", &state.fov, 60.0f, 130.0f, "%.0f");
    EndSection(ctx);

    ctx.Spacing();
    if (Button("Save", ButtonKind::Primary)) Persist();

    RenderTooltips(ctx);   // last, while the window's layout is still active
    EndWindow(ctx);
}
```

### `BeginWindowAt`

```cpp
bool BeginWindowAt(Context& ctx, const char* id, const char* title, const Rect& bounds,
                   bool* open = nullptr, Icon appIcon = Icon::None);
```

The same window, positioned by the caller instead of auto-centered. Use it when
the window has to sit at a remembered position, or be drawn into a viewport that
is not the full display.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source. Unique among calls in the same scope. |
| `title` | `const char*` | | Title bar text. |
| `bounds` | `const Rect&` | | Frame rectangle in pixels. |
| `open` | `bool*` | `nullptr` | Non-null draws a close button and is cleared on click. |
| `appIcon` | `Icon` | `Icon::None` | Glyph in a rounded accent tile. |

**Returns:** true when the window is open this frame.

### `EndWindow`

```cpp
void EndWindow(Context& ctx);
```

Pops the content layout and the window's z layer.

**Returns:** nothing.

{% hint style="warning" %}
Call it exactly when `BeginWindow` or `BeginWindowAt` returned true. It is
safe to call with no matching `Begin`, so a `false` return is not a crash, but
a `true` return with no `End` leaks the layout region and the z layer.
{% endhint %}

## Sections

### `BeginSection`

```cpp
bool BeginSection(Context& ctx, const char* title, bool* open = nullptr,
                  const char* subtitle = nullptr, Icon icon = Icon::None);
```

A collapsible card with a header bar. Clicking the header toggles the open
state. `BeginSection` takes no child height: the card is painted behind the body
at `EndSection`, where the height is known, by splicing a box into the index
buffer at the checkpoint `BeginSection` captured.

Draw the body unconditionally. When the section is closed, the body is clipped
to a zero-height rect, so the layout works out to zero without a second code
path and without a flicker if the caller forgets to check.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `title` | `const char*` | | Header text in the semibold font, also the widget id source. |
| `open` | `bool*` | `nullptr` | Open state. When null the widget keeps it in the context store instead. |
| `subtitle` | `const char*` | `nullptr` | Muted secondary text after the title. |
| `icon` | `Icon` | `Icon::None` | Accent glyph before the title, at 46 percent of the header height. |

**Returns:** the open state, after any header click.

**Example**

```cpp
using namespace argon::ui;

bool open = true;

BeginSection(ctx, "Renders", &open, "Draw settings", Icon::Cube);
    RowSlider(ctx, "FOV", &state.fov, 60.0f, 130.0f, "%.0f");
    RowSlider(ctx, "Box thickness", &state.boxThickness, 0.5f, 5.0f, "%.1f");
    RowToggle(ctx, "Smoothness", &state.smooth);
EndSection(ctx);
```

### `EndSection`

```cpp
void EndSection(Context& ctx);
```

Measures how tall the body actually was, paints the card behind it at the
checkpoint taken in `BeginSection`, and re-seats the layout cursor so a closed
section takes no vertical space.

**Returns:** nothing.

## Scrolling

### `BeginScrollArea`

```cpp
bool BeginScrollArea(Context& ctx, const char* id, f32 height, bool* scrollY = nullptr,
                     bool showScrollbar = true);
```

Opens a scrolling region that clips and offsets everything inside it. The wheel
is scaled by 52 pixels per notch, the fling decays at 11 per second, and the
offset is clamped to the region height minus one pixel. The scrollbar is painted
before the content so content scrolls over it.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and state key. Unique among calls in the same scope. |
| `height` | `f32` | | Region height in pixels. 0 or less fills the remaining content region. |
| `scrollY` | `bool*` | `nullptr` | Receives the current offset in pixels. |
| `showScrollbar` | `bool` | `true` | Draws the track and thumb once the offset passes half a pixel. |

**Returns:** true if the area scrolled this frame, from the wheel or from a
fling still settling. Use it to swallow the wheel when something above should
not also scroll.

{% hint style="warning" %}
The parameter is spelled `bool*` but the implementation writes a `float` offset
into it. Declare the variable as `f32` and read it as one. Passing an actual
`bool*` overruns the variable.
{% endhint %}

**Example**

```cpp
using namespace argon::ui;

f32 scroll = 0.0f;
BeginScrollArea(ctx, "body.scroll", 0.0f, &scroll, true);
    DrawAllSections(ctx, state);
EndScrollArea(ctx);
```

{% hint style="warning" %}
`BeginScrollArea` returns whether the area scrolled, not whether the region is
open. It is always open. Call `EndScrollArea` unconditionally, outside any `if`.
{% endhint %}

### `ScrollAreaContent`

```cpp
Rect ScrollAreaContent();
```

The inner content rect of the innermost open scroll area, already offset by the
current scroll position and clipped to the region. Use it when a widget needs
absolute coordinates rather than the layout cursor, such as a sticky header
drawn over a list.

**Returns:** the content rect, or an empty `Rect` when no scroll area is open.

### `EndScrollArea`

```cpp
void EndScrollArea(Context& ctx);
```

Pops the content layout and the clip rect.

**Returns:** nothing.

## Tabs and rails

### `TabBar`

```cpp
int TabBar(Context& ctx, const char* id, const char* const* tabs, int count, int* active,
           Rect* animatedIndicator = nullptr, Icon* tabIcons = nullptr);
```

An underlined tab bar. Every slot is the same width: the largest that fits
`count` slots in the available width, capped at the widest label plus its
padding. The underline is a `Vec2` animation, so it slides from one slot to the
next rather than cutting to it. Hovering a tab fills it with the soft accent
color.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and animation key. Unique among calls. |
| `tabs` | `const char* const*` | | Slot labels, also the per-tab id source. |
| `count` | `int` | | Number of slots. |
| `active` | `int*` | | Current index, written when the user clicks another slot. Clamped to the valid range. |
| `animatedIndicator` | `Rect*` | `nullptr` | Receives the animated underline rect. |
| `tabIcons` | `Icon*` | `nullptr` | Optional icon per slot. Only entries that are not `Icon::None` widen a slot. |

**Returns:** the active index, after any click.

{% hint style="info" %}
`TabBar` always draws the underline itself. The `animatedIndicator` out-param
publishes the same rect for a caller that wants to know where the underline is,
for example to fade it out during a transition, or to draw a highlight of its
own on top. Leaving it null changes nothing about what is rendered.
{% endhint %}

**Example**

```cpp
using namespace argon::ui;

static const char* kTabs[] = {"Visuals", "Enemy", "Local", "World", "Ambience"};
static const Icon kTabIcons[] = {Icon::Eye, Icon::Crosshair, Icon::User,
                                 Icon::Cube, Icon::Palette};
Rect indicator;
int tab = TabBar(ctx, "tabs", kTabs, 5, &state.tab, &indicator, kTabIcons);
ctx.Spacing();
switch (tab) {
case 1: DrawEnemyTab(ctx, state); break;
default: DrawVisualsTab(ctx, state); break;
}
```

### `Sidebar`

```cpp
int Sidebar(Context& ctx, const char* id, const Icon* icons, int count, int* selected,
            f32 width = 0.0f, const int* badges = nullptr);
```

An icon rail. The rail fills the available height and centers its items
vertically when it is taller than they are. It reports its own size to the
layout, so it stacks like any other item and the caller can open a region beside
it with `Context::BeginLayout` or `Context::SameLine` instead of doing
arithmetic.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source. Unique among calls. |
| `icons` | `const Icon*` | | One glyph per slot, also the per-item id source. |
| `count` | `int` | | Number of slots. |
| `selected` | `int*` | | Current index, written when the user clicks another slot. Clamped to the valid range. |
| `width` | `f32` | `0.0f` | Rail width in pixels. 0 uses `StyleVarId::SidebarWidth`. |
| `badges` | `const int*` | `nullptr` | Optional count per slot. Entries at 0 or below draw no chip. |

**Returns:** the selected index, after any click.

{% hint style="info" %}
Each slot's id is hashed from its own icon, so two slots carrying the same glyph
share an id and therefore share hover. Use `Context::PushID` around one of them
when a rail legitimately repeats an icon.
{% endhint %}

## Lists and grids

### `ListItem`

```cpp
struct ListItem {
    const char* label = nullptr;
    const char* detail = nullptr;    ///< Right-aligned secondary text.
    Icon icon = Icon::None;
    Color dotColor = Color(0, 0, 0, 0);
    u32 textureId = 0;               ///< Optional thumbnail.
    bool disabled = false;
};
```

One row of a `List`. The leading slot picks the first of texture, icon or dot
that is set, so a thumbnail always wins over a glyph, and a glyph always wins
over a dot.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | `nullptr` | Main text, also the row's id source. |
| `detail` | `const char*` | `nullptr` | Right-aligned secondary text in the small font. |
| `icon` | `Icon` | `Icon::None` | Leading glyph at 50 percent of the row height, drawn only when `textureId` is 0. |
| `dotColor` | `Color` | `Color(0, 0, 0, 0)` | Leading 7 pixel dot, drawn only when `textureId` is 0 and `icon` is `Icon::None` and alpha is above 0. |
| `textureId` | `u32` | `0` | Thumbnail handle from `Context::RegisterTexture`. 0 means no texture. |
| `disabled` | `bool` | `false` | Dims the label, drops the hover fill and blocks selection. |

**Returns:** nothing, it is a plain data struct.

### `List`

```cpp
int List(Context& ctx, const char* id, const ListItem* items, int count, int* selected,
         f32 rowHeight = 0.0f);
```

A vertical list filling the available width. The selected row gets the selection
fill, a hovered enabled row gets the card hover fill. Items are separate
interactive rows and the list advances the layout itself, so it stacks with
anything else.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Reserved. The implementation does not read it; each row's id comes from its label. |
| `items` | `const ListItem*` | | Array of at least `count` entries. |
| `count` | `int` | | Number of entries. |
| `selected` | `int*` | | Current index, written when the user clicks another row. |
| `rowHeight` | `f32` | `0.0f` | Row height in pixels. 0 uses `StyleVarId::ListItemHeight`. |

**Returns:** the selected index.

{% hint style="warning" %}
Rows are identified by their label text. Two rows with the same `label` share
an id, so they share hover, and only one of them can be clicked. When labels
repeat, push a distinct id scope per row.
{% endhint %}

### `Grid`

```cpp
void Grid(Context& ctx, int columns, f32 cellSize, f32 spacing,
          bool (*drawCell)(Context&, const Rect&, int index, void* userData), int count,
          void* userData = nullptr);
```

An evenly spaced gallery grid. Each cell is painted with a card background that
lightens on hover, then `drawCell` is called with the cell rect inset by
`StyleVarId::GridCellPadding`. The cursor is saved and restored around the call,
so a cell draws freely without disturbing the grid.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `columns` | `int` | | Column count. Values below 1 are treated as 1. |
| `cellSize` | `f32` | | Square cell side in pixels. |
| `spacing` | `f32` | | Gap between cells in pixels, both axes. |
| `drawCell` | `bool (*)(Context&, const Rect&, int, void*)` | | Draws one cell's contents. May be null to paint bare cards. |
| `count` | `int` | | Number of cells, laid out row-major. |
| `userData` | `void*` | `nullptr` | Passed through to `drawCell` along with the index. |

**Returns:** nothing.

{% hint style="info" %}
`drawCell` is a function pointer plus a `void*` rather than a `std::function`,
because the grid runs inside the per-frame hot path and a capturing lambda
there would allocate once per cell per frame. A non-capturing lambda converts
to a function pointer implicitly when it is passed directly. The callback's
return value is discarded: a cell is drawn or not drawn, never skipped.
{% endhint %}

### `InfoRow`

```cpp
void InfoRow(Context& ctx, const char* label, const char* value, Color dotColor = Color(0, 0, 0, 0),
             Icon icon = Icon::None);
```

A read-only key/value row with no interactive control: label dimmed on the left
of center, value in the small semibold font on the right. Always draws the
hairline separator underneath.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `label` | `const char*` | | Key text. |
| `value` | `const char*` | | Value text, right-aligned. |
| `dotColor` | `Color` | `Color(0, 0, 0, 0)` | Leading 7 pixel dot when alpha is above 0. |
| `icon` | `Icon` | `Icon::None` | Leading glyph at 48 percent of the row height. |

**Returns:** nothing.

## Text and numeric input

### `TextField`

```cpp
bool TextField(Context& ctx, const char* id, char* buffer, size_t bufferSize,
               const char* placeholder = nullptr, bool enabled = true, Icon icon = Icon::None);
bool TextField(Context& ctx, const char* id, std::string& value,
               const char* placeholder = nullptr, bool enabled = true, Icon icon = Icon::None);
```

Single-line editable text filling the available width. Clicking places the
caret at the nearest character boundary, the caret blinks while focused, and
the border and fill take their focus colors. Escape and Tab release focus, and
a click outside the field while focused releases it too. Backspace and Delete
erase a whole UTF-8 codepoint.

The `char*` overload copies into a `std::string` each frame and writes back only
on the frame something changed, so an untouched field costs no copy. Prefer the
`std::string&` overload unless you have to interoperate with a fixed buffer.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and state key. Unique among calls. |
| `buffer` / `value` | `char*` / `std::string&` | | Text storage. Written in place. |
| `bufferSize` | `size_t` | | Capacity of `buffer`, used by the `snprintf` write-back. Only the pointer overload has it. |
| `placeholder` | `const char*` | `nullptr` | Muted text shown while the field is empty. |
| `enabled` | `bool` | `true` | When false the field cannot take focus. |
| `icon` | `Icon` | `Icon::None` | Leading glyph at 52 percent of the field height. |

**Returns:** true on the frame the text changed.

{% hint style="warning" %}
The left and right arrow keys step the caret one byte at a time, so in UTF-8
text they can land inside a multi-byte codepoint. Editing at that position
produces broken text. Home and End, and clicking to place the caret, are
boundary-safe.
{% endhint %}

### `SearchField`

```cpp
bool SearchField(Context& ctx, const char* id, std::string& query,
                 const char* placeholder = "Search", bool enabled = true);
```

A `TextField` with the magnifier glyph pre-applied. There is no clear button in
this version; the caller adds one beside the field.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and state key. Unique among calls. |
| `query` | `std::string&` | | Query text, written in place. |
| `placeholder` | `const char*` | `"Search"` | Muted text shown while the query is empty. Null falls back to `"Search"`. |
| `enabled` | `bool` | `true` | When false the field cannot take focus. |

**Returns:** true on the frame the query changed.

### `DragFloat`

```cpp
bool DragFloat(Context& ctx, const char* id, f32* value, f32 speed = 0.1f, f32 vmin = 0.0f,
               f32 vmax = 0.0f, const char* fmt = "%.2f");
```

A numeric field you scrub horizontally. Press and drag; the value moves by the
pointer delta times `speed`. The field fills the available width, takes its
focused colors while it owns the drag, and releases the drag when the button
comes up.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and state key. Unique among calls. |
| `value` | `f32*` | | Current value, written on drag. |
| `speed` | `f32` | `0.1f` | Value change per pixel of horizontal drag. |
| `vmin` | `f32` | `0.0f` | Lower bound, applied only when `vmax` is greater than `vmin`. |
| `vmax` | `f32` | `0.0f` | Upper bound. Leave at 0 for an unbounded field. |
| `fmt` | `const char*` | `"%.2f"` | `printf` format for the readout. |

**Returns:** true on the frame `value` changed.

### `DragInt`

```cpp
bool DragInt(Context& ctx, const char* id, int* value, f32 speed = 1.0f, int vmin = 0, int vmax = 0);
```

The same scrub field with an integer payload, formatted with `"%d"`. Leave the
bounds at 0 for an unbounded field.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and state key. Unique among calls. |
| `value` | `int*` | | Current value, written on drag. |
| `speed` | `f32` | `1.0f` | Value change per pixel of horizontal drag. |
| `vmin` | `int` | `0` | Lower bound, applied only when `vmax` is greater than `vmin`. |
| `vmax` | `int` | `0` | Upper bound. |

**Returns:** true on the frame `value` changed.

### `TextBlock`

```cpp
void TextBlock(Context& ctx, const char* text, const Color& color = Color(0, 0, 0, 0),
               f32 fontSize = 0.0f);
```

Multi-line wrapped, read-only text. The block wraps to the available width,
advances the layout by the height it used, and draws nothing for null or empty
text.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `text` | `const char*` | | Text to wrap and draw. |
| `color` | `const Color&` | `Color(0, 0, 0, 0)` | Text color. Transparent alpha falls back to the muted theme text color. |
| `fontSize` | `f32` | `0.0f` | Font size in pixels. 0 uses `StyleVarId::TextSizeSmall`. |

**Returns:** nothing.

## Modals

### `BeginModal`

```cpp
bool BeginModal(Context& ctx, const char* id, const char* title, const Vec2& size,
                bool* open = nullptr, Icon icon = Icon::None, const char* closeLabel = nullptr);
```

A modal dialog. The frame is centered on the display, clamped to the theme's
minimum size and to the display minus 40 pixels, and animates in with a fade and
a 14 pixel rise. A `closeLabel` adds a footer strip carrying a Cancel button and
a primary button labelled with `closeLabel`, both 108 by 34. The content area
below the header becomes the active layout, so the body is drawn straight after
the call with no positioning.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and animation key. Unique among calls. |
| `title` | `const char*` | | Header text in the semibold title font. |
| `size` | `const Vec2&` | | Frame size in pixels, before clamping. |
| `open` | `bool*` | `nullptr` | Open state. Null keeps it in the context store. Escape clears it either way. |
| `icon` | `Icon` | `Icon::None` | Accent glyph at the left of the header. |
| `closeLabel` | `const char*` | `nullptr` | Text of the primary footer button. Null draws no footer at all. |

**Returns:** true while the modal is open. False when `open` was false on entry
or Escape cleared it this frame; in both of those cases the call returned before
pushing anything.

{% hint style="warning" %}
The backdrop is a real item registered at a higher z layer and the modal pushes
an input lock, so every widget beneath it stops taking presses while it is up.
The lock is pushed before the header close button and the footer buttons
register, so in the current implementation those two do not receive presses
either: `Context::ItemBehavior` suppresses the press while a lock is held.
Escape is the dismiss path that works.
{% endhint %}

{% hint style="warning" %}
Closing the modal from a button inside `BeginModal` would clear the flag and send
the return value false after the call had already pushed the z layer, the input
lock and the body layout. The usual `if (BeginModal(...)) { ... EndModal(...); }`
shape would then skip the `End` and leak all three. Drive the open flag from
outside the modal and never clear it from a button drawn inside it.
{% endhint %}

**Example**

```cpp
using namespace argon::ui;

if (BeginModal(ctx, "argon.modal", "Appearance", Vec2(560.0f, 360.0f), &state.modalOpen,
               Icon::Palette, "Apply")) {
    TextField(ctx, "modal.search", state.search, "Search effects");
    ctx.Spacing();

    static const char* kEffects[] = {"Glow", "Outline", "Chams", "Radar"};
    SegmentedControl("modal.effect", kEffects, 4, &state.effect);
    ctx.Spacing();

    RowToggle(ctx, "Animate", &state.animate);
    EndModal(ctx);
}
```

### `EndModal`

```cpp
void EndModal(Context& ctx);
```

Measures the body, paints a card behind it when it used more than a pixel of
height, then pops the input lock and the z layer.

**Returns:** nothing.

### `ModalContent`

```cpp
Rect ModalContent();
```

The body rect of the innermost open modal, the region between the header and
the footer. The modal already makes this the active layout, so it is only
needed for an extra nested layout or for absolute positioning.

**Returns:** the body rect, or an empty `Rect` when no modal is open.

### `ModalFooter`

```cpp
Rect ModalFooter();
```

The footer strip of the innermost open modal, the area that carries the Cancel
button and the primary button labelled with `closeLabel`. It is a zero-height
rect when `BeginModal` was called without a `closeLabel`.

**Returns:** the footer rect, or an empty `Rect` when no modal is open.

### `ModalHeader`

```cpp
Rect ModalHeader();
```

The header strip of the innermost open modal. ArgonUI draws the title, icon and
close button itself; use this rect only to place something else in the same
row.

**Returns:** the header rect, or an empty `Rect` when no modal is open.

## Popups

### `PopupList`

```cpp
int PopupList(Context& ctx, const char* id, const Vec2& anchor, const char* const* labels,
              int count, int* selected, f32 width = 0.0f, bool* open = nullptr,
              const Icon* icons = nullptr);
```

The dropdown list behind `Combo` and `ContextMenu`, usable on its own. It is
placed below `anchor`, flips above it when that would run off the display, and
then clamps to the display. It slides down and fades in as it opens and stops
drawing below 5 percent of the open animation, so a dismissed popup costs
nothing. It publishes its bounds so the next frame turns an outside click into a
dismissal instead of a press on whatever is underneath.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Widget id source and animation key. Unique among calls. |
| `anchor` | `const Vec2&` | | Point the popup hangs from. |
| `labels` | `const char* const*` | | Entry labels, also the per-row id source. |
| `count` | `int` | | Number of entries. |
| `selected` | `int*` | | Index to fill with the selection color. Null draws no selected fill. |
| `width` | `f32` | `0.0f` | Popup width in pixels. 0 measures the widest label with a 120 pixel floor. |
| `open` | `bool*` | `nullptr` | Open flag. Null means the popup never opens. |
| `icons` | `const Icon*` | `nullptr` | Optional icon per entry. Only entries that are not `Icon::None` are drawn with one. |

**Returns:** the clicked index, or -1 when nothing was clicked, the popup is
still opening, or it is closed.

### `ContextMenu`

```cpp
int ContextMenu(Context& ctx, const char* id, const char* const* labels, int count,
                const Icon* icons = nullptr, bool* open = nullptr);
```

A right-click menu anchored at the cursor. A right click anywhere the input is
not locked toggles it and records the cursor position as the anchor. It does
nothing while a modal or another popup holds an input lock.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `id` | `const char*` | | Passed to `PopupList` as its id. Unique among calls. |
| `labels` | `const char* const*` | | Entry labels. |
| `count` | `int` | | Number of entries. |
| `icons` | `const Icon*` | `nullptr` | Optional icon per entry. |
| `open` | `bool*` | `nullptr` | Caller-owned open flag. |

**Returns:** the clicked index, or -1.

{% hint style="warning" %}
Passing `open` hands the flag to the caller, but the right-click handler only
updates the context state store, never the caller's flag. A non-null `open`
therefore stays false and the menu never opens from a right click. Leave it
null and read the return value.
{% endhint %}

### `RenderTooltips`

```cpp
void RenderTooltips(Context& ctx);
```

Draws the tooltip queued this frame, the last request in the queue, clamped to
the display with 4 pixel margins. Call it once per frame, after every widget
that could have queued one, so the tooltip is not covered by anything drawn
later.

**Returns:** nothing.

{% hint style="warning" %}
Skipping this call loses every tooltip queued in the frame. It does not drain the
queue either, so calling it twice draws the tooltip twice. Call `ClearTooltips`
once per frame alongside `Context::NewFrame` to start from an empty queue.
{% endhint %}

## See also

- [Controls](controls.md)
- [Icons](icons.md)
- [Internal helpers](internal.md)
- [Writing a widget](../guides/writing-a-widget.md)
