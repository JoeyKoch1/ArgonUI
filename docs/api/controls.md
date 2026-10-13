# Controls

The interactive controls: buttons, toggles, sliders, badges and the `Row`
helper that the reference UI is built from. Every widget takes its colors and
metrics from the active `Style`, animates hover and press through the shared
helpers so the whole interface moves at one rate, and reports its own size so
stacking widgets reads as a list instead of coordinate arithmetic.

Header: [`include/argonui/Widgets/Controls.h`](../../include/argonui/Widgets/Controls.h).

## How state is stored

Widgets keep nothing of their own. Toggle positions, animation phase and open
flags live in the `Context` state store keyed by the widget's hashed id, so a
caller holds a plain `bool`, a `float` or an `int` and nothing more. Two
consequences follow from that:

- A control that takes a `const char* id` (`SegmentedControl`, `Combo`) needs
  that id to be unique among its peers. A control that derives its id from its
  label needs the label to be unique.
- A control whose id comes from a fixed string (`IconButton`, `ToggleSwitch`)
  gives every instance in the same id scope the same id. Wrap each one in its
  own `Context::PushID` scope.

## Buttons

### `ButtonKind`

```cpp
enum class ButtonKind {
    Primary = 0,   ///< Solid accent, dark text. "Save", "Equip".
    Secondary,     ///< Raised surface + hairline border. "Default".
    Ghost,         ///< No surface until hovered. Inline actions, close.
    Danger,        ///< Destructive confirmation.
    Warning,
};
```

The visual variants the button family understands. Each resolves to a
`ButtonColors` set, and only `Primary` carries a glow.

**Returns:** nothing, it is an enumeration.

### `IconSlot`

```cpp
enum class IconSlot {
    None = 0,
    Left,
    Right,
};
```

Which side of the label an icon sits on in `ButtonWithIcon`.

**Returns:** nothing, it is an enumeration.

### `ButtonColors`

```cpp
struct ButtonColors {
    Color bg, bgHovered, bgActive, border, borderHovered, fg, fgDisabled;
    f32 borderWidth = 0.0f;
    f32 radius = 0.0f;
    f32 height = 0.0f;
    f32 paddingX = 0.0f;
};
```

The resolved color and metric set for one variant. `bg` is blended toward
`bgHovered` by the hover animation and then toward `bgActive` by the press
animation. `borderHovered` replaces `border` once the hover animation passes
0.01. `fgDisabled` is the color a disabled button draws its label in.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `bg` | `Color` | | Resting fill. |
| `bgHovered` | `Color` | | Fill at full hover. |
| `bgActive` | `Color` | | Fill at full press. |
| `border` | `Color` | | Resting border. |
| `borderHovered` | `Color` | | Border once hover animation passes 0.01. |
| `fg` | `Color` | | Label and icon color. |
| `fgDisabled` | `Color` | | Label color when `enabled` is false. |
| `borderWidth` | `f32` | `0.0f` | Border thickness. `Primary`, `Ghost`, `Danger` and `Warning` force 0. |
| `radius` | `f32` | `0.0f` | Corner radius in pixels. |
| `height` | `f32` | `0.0f` | Default button height. |
| `paddingX` | `f32` | `0.0f` | Horizontal padding, added to both sides of the label. |

**Returns:** nothing, it is a plain data struct.

### `ResolveButton`

```cpp
ButtonColors ResolveButton(ButtonKind kind, const Style& style);
```

Resolves the color and metric set for `kind` against `style`. `Danger` and
`Warning` lighten their hover fill toward white by 14 percent rather than taking
a dedicated theme entry, so a theme only has to supply the base color.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `kind` | `ButtonKind` | | Variant to resolve. |
| `style` | `const Style&` | | Style the colors and vars come from. |

**Returns:** the `ButtonColors` for that variant.

{% hint style="info" %}
Custom button chrome is easiest to build by calling `ResolveButton` for the
state you want, then painting it yourself. The returned set already has the
right radius, height and padding for the current theme.
{% endhint %}

### `Button`

```cpp
bool Button(const char* label, ButtonKind kind = ButtonKind::Secondary,
            const Vec2& size = Vec2(0.0f, 0.0f), bool enabled = true);
```

Standard text button, sized to its label when `size` is zero. A `Primary`
button gains a soft accent halo that strengthens on hover; the falloff is in
the shader, so it costs one primitive and no blur pass.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Button text, also the widget id source. May be null for an iconless, textless button. |
| `kind` | `ButtonKind` | `ButtonKind::Secondary` | Visual variant. |
| `size` | `const Vec2&` | `Vec2(0.0f, 0.0f)` | Explicit width and height in pixels. A component at or below 0 is auto-sized. |
| `enabled` | `bool` | `true` | When false the button draws at half style alpha and never reports a click. |

**Returns:** true on the frame the button is clicked.

{% hint style="warning" %}
`label` is the id. Two `Button` calls with the same text in one id scope share
an id, so hover, press and animation state bleed between them. Give them
different labels, or wrap them in `Context::PushID`.
{% endhint %}

### `ButtonWithIcon`

```cpp
bool ButtonWithIcon(const char* label, Icon icon, ButtonKind kind = ButtonKind::Secondary,
                    IconSlot slot = IconSlot::Left, const Vec2& size = Vec2(0.0f, 0.0f),
                    bool enabled = true);
```

Text button with a stroke icon in `slot`. The icon is sized at 56 percent of
the button height, the gap between icon and label at 26 percent, and both are
stroked at the larger of 1.2 pixels and 5.5 percent of the button height.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Button text, also the widget id source. |
| `icon` | `Icon` | | Glyph to draw. `Icon::None` falls back to a plain centered label. |
| `kind` | `ButtonKind` | `ButtonKind::Secondary` | Visual variant. |
| `slot` | `IconSlot` | `IconSlot::Left` | Which side of the label the icon sits on. |
| `size` | `const Vec2&` | `Vec2(0.0f, 0.0f)` | Explicit size in pixels. Auto-size includes the icon and gap. |
| `enabled` | `bool` | `true` | When false the button is inert. |

**Returns:** true on the frame the button is clicked.

**Example**

```cpp
using namespace argon::ui;

f32 h = ctx.GetVar(StyleVarId::ButtonHeight);

bool save = ButtonWithIcon("Save", Icon::Save, ButtonKind::Primary, IconSlot::Left,
                           Vec2(112.0f, h));
bool more = ButtonWithIcon("Browse", Icon::FolderOpen, ButtonKind::Secondary,
                           IconSlot::Right, Vec2(132.0f, h));

if (save) Persist();
if (more) OpenBrowser();
```

### `IconButton`

```cpp
bool IconButton(Icon icon, const char* tooltip = nullptr, ButtonKind kind = ButtonKind::Ghost,
                f32 size = 0.0f, bool enabled = true, const Color* tint = nullptr);
```

Square icon-only button. The glyph occupies 54 percent of the button and is
stroked at the larger of 1.2 pixels and 6 percent of the size. A `tooltip` is
queued while the button is hovered and floats above everything else when
`RenderTooltips` runs.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `icon` | `Icon` | | Glyph to draw, centered. |
| `tooltip` | `const char*` | `nullptr` | Queued tooltip while hovered. Null or empty draws none. |
| `kind` | `ButtonKind` | `ButtonKind::Ghost` | Visual variant. `Ghost` is the usual choice. |
| `size` | `f32` | `0.0f` | Side length in pixels. 0 uses the resolved button height. |
| `enabled` | `bool` | `true` | When false the button is inert. |
| `tint` | `const Color*` | `nullptr` | Overrides the resting glyph color before the hover tint is applied. |

**Returns:** true on the frame the button is clicked.

{% hint style="warning" %}
The id is the fixed string `"##iconbutton"`, so every `IconButton` drawn in the
same id scope shares hover, press and animation state. Give each one its own
`Context::PushID` scope. The same is true of `ToggleSwitch`.
{% endhint %}

### `Row`

```cpp
bool Row(Context& ctx, const char* label, const RowOptions& options,
         bool (*drawControl)(Context&, const Rect&, void* userData), void* userData = nullptr);
```

The workhorse of the reference UI: a label on the left, a control on the right,
a hairline separator underneath, and an optional overflow button between them.
`drawControl` is handed the rect the control should draw into, and the context
saves and restores the layout cursor around the call, so a control placed in a
row never advances the outer layout.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ctx` | `Context&` | | Context to draw into. |
| `label` | `const char*` | | Left-hand text. May be null when the control stands alone. |
| `options` | `const RowOptions&` | | Decoration and metrics for this row. |
| `drawControl` | `bool (*)(Context&, const Rect&, void*)` | | Draws the right-hand control into the rect it is given and returns whether it changed. |
| `userData` | `void*` | `nullptr` | Passed through to `drawControl`. |

**Returns:** whatever `drawControl` returned.

{% hint style="warning" %}
The row registers its own bounds with `ItemFlags::NoInteraction`, so it never
steals hover or a click from the control inside it. Keep it that way: a custom
control that calls `Context::ItemBehavior` over the whole `Row` rect will make
both items hover at once.
{% endhint %}

**Example**

```cpp
using namespace argon::ui;

struct SwatchRef { Color* c; };

bool DrawSwatch(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<SwatchRef*>(ud);
    const f32 s = Min(22.0f, r.Height() * 0.8f);
    const Rect box(r.max.x - s, r.GetCenter().y - s * 0.5f,
                   r.max.x, r.GetCenter().y + s * 0.5f);
    const ItemResult ir = ctx.ItemBehavior(ctx.GetID("swatch"), box);
    ctx.AddBox(box, *ref->c, CornerRadius(6.0f), 1.5f,
               ctx.GetColor(StyleColorId::White).WithAlpha(0.35f));
    if (ir.hovered) ctx.AddBox(box, Color(1, 1, 1, 0.10f), CornerRadius(6.0f));
    return ir.clicked;
}

// ...

SwatchRef ref{&state.primary};
RowOptions o;
o.separatorAfter = false;
if (Row(ctx, "Primary", o, DrawSwatch, &ref)) {
    // The swatch was clicked.
}
```

### `RowOptions`

```cpp
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
```

Per-row decoration and metrics. The icon and the dot both shift the label right,
and both can be combined.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `overflowButton` | `bool` | `false` | Draws a `"..."` button at 72 percent of the row height, immediately to the left of the control. |
| `separatorAfter` | `bool` | `true` | Draws the hairline under the row, inset by `SeparatorInset`. |
| `highlightOnHover` | `bool` | `true` | Tints the whole row background at 55 percent when the pointer is over it, ignoring overlap. |
| `icon` | `Icon` | `Icon::None` | Glyph before the label, at 50 percent of the row height. |
| `dotColor` | `Color` | `Color(0, 0, 0, 0)` | Fills a 7 pixel dot before the label when alpha is above 0. |
| `height` | `f32` | `0.0f` | Row height in pixels. 0 uses `StyleVarId::RowHeight`. |
| `minControlWidth` | `f32` | `0.0f` | Reserved. `Row` does not read it today. |
| `labelFontSize` | `f32` | `0.0f` | Label font size. 0 uses `StyleVarId::TextSize`. |

**Returns:** nothing, it is a plain data struct.

## Toggles and inputs

### `Checkbox`

```cpp
bool Checkbox(const char* label, bool* value, bool enabled = true);
```

Square checkbox with an animated tick. The box, its tick and the border color
all animate at `StyleVarId::ToggleRate`, and the tick only draws once the
animation passes 0.02 so an unchecked box stays clean.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Text after the box, also the widget id source. May be null for a bare box. |
| `value` | `bool*` | | Toggled in place on click. Null draws the box without behavior. |
| `enabled` | `bool` | `true` | When false the box is inert and drawn dimmed. |

**Returns:** true on the frame it is clicked.

### `ToggleSwitch`

```cpp
bool ToggleSwitch(bool* value, bool enabled = true, f32 width = 0.0f, f32 height = 0.0f);
```

Pill switch with a sliding knob. Pressing nudges the whole track sideways by
1.2 pixels, which reads as physical without disturbing the knob, and the knob
stays on its animated position.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `value` | `bool*` | | Toggled in place on click. Null draws the switch without behavior. |
| `enabled` | `bool` | `true` | When false the switch is inert and the knob takes the disabled color. |
| `width` | `f32` | `0.0f` | Track width in pixels. 0 uses `StyleVarId::ToggleWidth`. |
| `height` | `f32` | `0.0f` | Track height in pixels. 0 uses `StyleVarId::ToggleHeight`. |

**Returns:** true on the frame it is clicked.

{% hint style="info" %}
The header comment for this function mentions a `label` parameter. There is none
in the declaration or the implementation. Draw the bare switch and put the
label in a `Row`.
{% endhint %}

### `SliderFloat`

```cpp
bool SliderFloat(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt = "%.2f",
                 bool enabled = true);
```

Horizontal slider. Clicking anywhere on the rail jumps the thumb there and
dragging scrubs from that point. The displayed position is smoothed at
`StyleVarId::ScrollRate`, so a programmatic step is not a jump. The thumb gains
an accent halo while it is held.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Text left of the rail, also the widget id source. May be null or empty for a bare rail. |
| `value` | `f32*` | | Current value, written on interaction. Null renders read-only. |
| `vmin` | `f32` | | Lower bound. |
| `vmax` | `f32` | | Upper bound. Equal to `vmin` renders at the far left. |
| `fmt` | `const char*` | `"%.2f"` | `printf` format for the value readout on the right. Null or empty draws no readout. |
| `enabled` | `bool` | `true` | When false the rail is inert. |

**Returns:** true on the frame `value` changed.

### `SliderFloatVertical`

```cpp
bool SliderFloatVertical(const char* label, f32* value, f32 vmin, f32 vmax,
                         const char* fmt = "%.2f", f32 height = 120.0f, bool enabled = true);
```

The same slider turned on its side, for the rare panel that wants one. Value 1
is at the top. No label is drawn beside the rail; a vertical panel puts its own
label above it.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Widget id source. Not drawn next to the rail. |
| `value` | `f32*` | | Current value, written on interaction. |
| `vmin` | `f32` | | Lower bound, at the bottom of the rail. |
| `vmax` | `f32` | | Upper bound, at the top of the rail. |
| `fmt` | `const char*` | `"%.2f"` | `printf` format for the readout. Null or empty draws none. |
| `height` | `f32` | `120.0f` | Rail height in pixels. |
| `enabled` | `bool` | `true` | When false the rail is inert. |

**Returns:** true on the frame `value` changed.

### `SliderInt`

```cpp
bool SliderInt(const char* label, int* value, int vmin, int vmax, const char* suffix = nullptr,
               bool enabled = true);
```

Integer slider. It drives the shared float implementation with a `"%d"` format
that `suffix` is appended to, so the readout reads `90` or `90fps` depending on
what you pass.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Text left of the rail, also the widget id source. |
| `value` | `int*` | | Current value, written on interaction. |
| `vmin` | `int` | | Lower bound. |
| `vmax` | `int` | | Upper bound. |
| `suffix` | `const char*` | `nullptr` | Appended to the integer readout. |
| `enabled` | `bool` | `true` | When false the rail is inert. |

**Returns:** true on the frame `value` changed.

{% hint style="warning" %}
The underlying rail works in floats, so a drag that lands between two integers
truncates toward zero when it is written back. Snap `vmin` and `vmax` to the
range you actually want, or the stored value can sit one step below what the
readout shows after a drag.
{% endhint %}

## Display

### `ProgressBar`

```cpp
void ProgressBar(f32 fraction, const char* label = nullptr, const Color& color = Color(0, 0, 0, 0));
```

Determinate progress bar 6 pixels tall, filling the available width. A `label`
adds a second row 4 pixels below the bar, drawn in the muted text color.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `fraction` | `f32` | | Progress from 0 to 1. Clamped, so a value above 1 draws full. |
| `label` | `const char*` | `nullptr` | Optional caption under the bar. |
| `color` | `const Color&` | `Color(0, 0, 0, 0)` | Fill color. Transparent alpha falls back to the theme accent. |

**Returns:** nothing.

### `Badge`

```cpp
void Badge(const char* text, const Color& color = Color(0, 0, 0, 0));
```

Small tinted pill for counts, statuses and tags. The pill uses `AccentSoft` and
the text uses `Accent` when no color is passed.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | Pill text, also the measurement source. |
| `color` | `const Color&` | `Color(0, 0, 0, 0)` | When alpha is above 0, this color is used for both the pill fill and the text. |

**Returns:** nothing.

{% hint style="warning" %}
A non-transparent `color` becomes the pill fill *and* the text color, because
both branches read the same argument. The text disappears into the fill. To
get a colored pill with readable text, draw `Badge` for the pill and put the
text next to it, or pass a color with a low alpha and accept the tint.
{% endhint %}

### `BadgeSize`

```cpp
Vec2 BadgeSize(const char* text, const Color& color = Color(0, 0, 0, 0));
```

The size `Badge` will occupy, without drawing it. `color` is accepted so the
signature matches `Badge`, and is not read.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | Pill text to measure. |
| `color` | `const Color&` | `Color(0, 0, 0, 0)` | Ignored. |

**Returns:** the pill width and height in pixels.

## Pickers

### `SegmentedControl`

```cpp
int SegmentedControl(const char* id, const char* const* options, int count, int* selected,
                     f32 itemWidth = 0.0f);
```

A rounded track with a pill that slides to the selected slot. The slide is a
`Vec2` animation at 1.4 times the hover rate, so it is frame-rate safe and
crosses the gaps between slots rather than cutting.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `const char*` | | Unique among calls in the same scope. Not drawn. |
| `options` | `const char* const*` | | Slot labels, measured to auto-size. |
| `count` | `int` | | Number of slots. |
| `selected` | `int*` | | Current index, written when the user picks another slot. Clamped to the valid range. |
| `itemWidth` | `f32` | `0.0f` | Slot width in pixels. 0 sizes every slot to the widest label plus 20 pixels of padding. |

**Returns:** 1 when the selection changed this frame, 0 when it did not. It does
not return the index; the new index is written to `selected`.

**Example**

```cpp
using namespace argon::ui;

static const char* kModes[] = {"Visible", "Behind wall"};
int mode = 1;

if (SegmentedControl("esp.mode", kModes, 2, &mode) > 0) {
    // mode is already the new index.
}
```

### `Combo`

```cpp
int Combo(const char* id, const char* const* options, int count, int* selected, f32 width = 0.0f,
          const char* placeholder = nullptr);
```

Dropdown built on `PopupList`. Clicking the field toggles the list, the field
and border take their focused colors while the list is open, and the chevron
switches from down to up. The list animates open and slides down as it fades
in; below 5 percent of the open animation the popup stops drawing entirely.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `const char*` | | Unique among calls in the same scope. Not drawn. |
| `options` | `const char* const*` | | Entry labels. |
| `count` | `int` | | Number of entries. |
| `selected` | `int*` | | Current index, written when the user picks another entry. An out-of-range value shows the placeholder. |
| `width` | `f32` | `0.0f` | Field width in pixels. 0 measures the widest entry, with a 120 pixel floor. |
| `placeholder` | `const char*` | `nullptr` | Text shown when `selected` is out of range. Drawn in the muted color. |

**Returns:** the picked index, or -1 when the popup is closed or dismissed. The
picked index is also written to `selected`, so the return value is for the
"the user chose something" case rather than for the assignment.

{% hint style="warning" %}
`Combo` owns its open flag in the context state store, so the caller does not
track it. It does close itself on an outside click: an open popup publishes its
bounds and the next `Context::NewFrame` turns a click outside them into a
global press suppression, so the click does not also press the widget
underneath.
{% endhint %}

### `Breadcrumbs`

```cpp
void Breadcrumbs(const char* const* items, int count, int* hoveredIndex = nullptr);
```

Path-style breadcrumbs separated by chevrons, with the last item in full text
and the rest dimmed. Each item is its own interactive item, and hovering one
brightens it. A filler item fills the rest of the row so the strip reads as a
toolbar rather than floating text.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `items` | `const char* const*` | | Path components, left to right. |
| `count` | `int` | | Number of components. |
| `hoveredIndex` | `int*` | `nullptr` | Receives the hovered component, or -1 when nothing is hovered. |

**Returns:** nothing.

## Row wrappers

These place the most common controls in a `Row`. They all build a `RowOptions`
with defaults and hand the control a right-aligned rect.

### `RowToggle`

```cpp
bool RowToggle(const char* label, bool* value, bool enabled = true);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Row label, also the row id source. |
| `value` | `bool*` | | Toggled in place by the switch. |
| `enabled` | `bool` | `true` | Accepted for symmetry. The implementation does not read it and the switch is always live. |

**Returns:** true on the frame the switch was clicked.

### `RowSlider`

```cpp
bool RowSlider(const char* label, f32* value, f32 vmin, f32 vmax, const char* fmt = "%.2f",
               bool enabled = true);
```

The rail fills the whole control slot and the thumb radius is clamped to a
sane size even in a very wide row. `label` is the row label; the slider itself is
drawn without one.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Row label. |
| `value` | `f32*` | | Current value, written on interaction. |
| `vmin` | `f32` | | Lower bound. |
| `vmax` | `f32` | | Upper bound. |
| `fmt` | `const char*` | `"%.2f"` | `printf` format for the readout. |
| `enabled` | `bool` | `true` | Accepted for symmetry. The implementation does not read it. |

**Returns:** true on the frame `value` changed.

### `RowCombo`

```cpp
bool RowCombo(const char* id, const char* const* options, int count, int* selected);
```

A row with no label and a right-aligned combo between 110 and 160 pixels wide.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `const char*` | | Passed to `Combo` as its id. Unique among calls. |
| `options` | `const char* const*` | | Entry labels. |
| `count` | `int` | | Number of entries. |
| `selected` | `int*` | | Current index, written when the user picks another entry. |

**Returns:** true on the frame the selection changed.

### `RowButton`

```cpp
bool RowButton(const char* label, const char* buttonLabel, ButtonKind kind = ButtonKind::Secondary,
               Icon icon = Icon::None, IconSlot slot = IconSlot::Left);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Row label. |
| `buttonLabel` | `const char*` | | Text on the 120 pixel wide button. |
| `kind` | `ButtonKind` | `ButtonKind::Secondary` | Visual variant. |
| `icon` | `Icon` | `Icon::None` | Glyph on the button. |
| `slot` | `IconSlot` | `IconSlot::Left` | Which side of the button label the icon sits on. |

**Returns:** true on the frame the button was clicked.

## Tooltips

Tooltips are queued during the frame and drawn at the end, so they float above
every widget no matter how the layout is ordered.

### `ShowTooltip`

```cpp
void ShowTooltip(const char* text, const Vec2& anchor);
void ShowTooltip(const char* text, const Rect& anchor);
```

Queues a tooltip for this frame. The point overload is the rectangle overload
with a zero-sized rectangle at that point, so a caller with a screen position
and a caller with a widget rect end up in the same queue.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | Tooltip text. Null or empty is ignored. |
| `anchor` | `const Vec2&` / `const Rect&` | | Where the tooltip hangs from. The tooltip is centered on the anchor and placed 8 pixels below its bottom edge. |

**Returns:** nothing.

{% hint style="info" %}
Only the last request in a frame is drawn. When two widgets queue a tooltip in
the same frame, the one queued last wins, which is the topmost hovered widget
in a normal layout.
{% endhint %}

### `ClearTooltips`

```cpp
void ClearTooltips();
```

Empties the queue. Nothing in the library calls it for you: call it once per
frame next to `Context::NewFrame`, or a tooltip queued in one frame is still
there in the next one.

**Returns:** nothing.

{% hint style="warning" %}
The header comment says `Context::NewFrame` calls this. It does not. The demo
application calls `ClearTooltips()` itself, immediately before `ctx.NewFrame`,
and a host application has to do the same.
{% endhint %}

## See also

- [Containers, pickers and overlays](widgets.md)
- [Icons](icons.md)
- [Internal helpers](internal.md)
- [Writing a widget](../guides/writing-a-widget.md)
