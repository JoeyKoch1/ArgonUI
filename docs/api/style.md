# Style

`Theme` holds the design decisions. `Style` holds a mutable copy of them plus a
push/pop stack, so a widget can be themed per-window, per-section or for the
duration of a single call without leaking the change. Widgets never read the
theme directly; they read the active style.

This page documents [`include/argonui/Theme/Style.h`](../../include/argonui/Theme/Style.h),
whose values are seeded by [`Theme`](theme.md).

## The style stack

`Style` holds two fixed-size arrays indexed by `StyleColorId` and `StyleVarId`,
plus two stacks of saved previous values. `PushColor` records the current value
and installs a new one; `PopColors` restores it. The colour and var stacks are
completely independent: a push on one has no effect on the depth of the other.

The practical shape is push, draw, pop.

```cpp
ctx.PushStyleColor(StyleColorId::Accent, Color(1, 0, 0));
ctx.PushStyleVar(StyleVarId::ButtonHeight, 44.0f);
// ... draw, everything drawn here is red with taller buttons
ctx.PopStyleColor(1);
ctx.PopStyleVar(1);
```

{% hint style="warning" %}
Push and pop must balance. `ColorDepth()` and `VarDepth()` report the current
depths, so a leak is easy to spot in a debug overlay. A leaked push does not
corrupt memory, it just leaves the wrong colour in place for the rest of the
frame, which is much harder to diagnose than a crash.
{% endhint %}

## The Alpha var

`StyleVarId::Alpha` multiplies through the drawing primitives, so
`PushStyleVar(StyleVarId::Alpha, 0.5f)` fades everything a scope draws.

Which functions honour it:

| Function | Honours `Alpha` |
|---|---|
| `Context::AddBox` | Yes, fill, border and glow alpha all multiplied. |
| `Context::AddLine` | Yes. |
| `Context::AddCircle` | Yes. |
| `Context::AddImage` | Yes, the tint alpha is multiplied. |
| `Context::AddText` | No, text colour is used verbatim. |
| `Context::AddTextCentered` | No. |
| `Context::AddTextRightAligned` | No. |
| `DrawIcon` | No, it multiplies only its own `alpha` argument. |

The split is why the disabled widgets in `Controls.cpp` and `Pickers.cpp` push
`Alpha` scaled by half *and* read colours through `Context::Color`: the boxes
fade with the var, and the labels pick up a faded style colour of their own. If
you set `Alpha` and expect text to fade with it, that is the behaviour you will
get: shapes fade, glyphs do not.

{% hint style="info" %}
`StyleVarId::DisabledAlpha` defaults to 0.45 but nothing in the current widgets
reads it; they use a hardcoded 0.5 factor. It is a usable value to push yourself.
{% endhint %}

## StyleColorId

Colours the widgets read. Every member is seeded from the active `Theme`'s
`Palette` by `Style::Reset`, and every one can be overridden with `SetColor` or
temporarily replaced with `PushColor`.

| Member | Value | Dark default | Meaning |
|---|---|---|---|
| `Text` | 0 | `0xF2F4F7` | Body text. |
| `TextDim` | 1 | `0x9AA1AC` | Secondary text and inactive icons. |
| `TextMuted` | 2 | `0x656C77` | Tertiary text, hints, disabled captions. |
| `TextDisabled` | 3 | `0x454B54` | Text of a disabled control. |
| `WindowBg` | 4 | `0x0B0C0E` @ 0.98 | Main window surface. |
| `WindowBorder` | 5 | `0x23262B` | Window outline. |
| `SidebarBg` | 6 | `0x101216` | Sidebar surface. |
| `TitleBarBg` | 7 | `0x0E1013` | Title bar surface. |
| `PanelBg` | 8 | `0x111317` | Dropdown and popover surface. |
| `CardBg` | 9 | `0x16181C` | Card surface. |
| `CardBgHover` | 10 | `0x1B1E23` | Card surface while hovered, and hover fill for ghost buttons. |
| `CardBorder` | 11 | `0x25282E` | Card outline. |
| `SectionHeaderBg` | 12 | `0x191C20` | Section header strip. |
| `FieldBg` | 13 | `0x1D2025` | Input field surface. |
| `FieldBgHover` | 14 | `0x23262C` | Input field surface while hovered. |
| `FieldBorder` | 15 | `0x2B2F36` | Input field outline. |
| `FieldBorderFocus` | 16 | `0x3A3F48` | Input field outline while focused. |
| `Accent` | 17 | `0xA3E635` | Accent colour: primary buttons, active tab, sidebar marker. |
| `AccentHover` | 18 | `0xB4F04D` | Accent while hovered. |
| `AccentPressed` | 19 | `0x8CC72B` | Accent while pressed. |
| `AccentSoft` | 20 | `0xA3E635` @ 0.16 | Translucent accent for highlights and disabled primary labels. |
| `OnAccent` | 21 | `0x10150A` | Text drawn on top of a solid accent fill. |
| `Danger` | 22 | `0xF2555A` | Destructive actions. |
| `DangerSoft` | 23 | `0xF2555A` @ 0.16 | Translucent danger, hover fill for destructive ghost buttons. |
| `Warning` | 24 | `0xF5B94C` | Warning states. |
| `Success` | 25 | `0x4ADE80` | Success states. |
| `Info` | 26 | `0x60A5FA` | Informational states. |
| `Track` | 27 | `0x33373F` | Slider and toggle track. |
| `TrackHover` | 28 | `0x3D424B` | Track while hovered. |
| `Thumb` | 29 | `0xF2F4F7` | Slider thumb, toggle knob. |
| `ThumbAccent` | 30 | `0xA3E635` | Accent thumb used on filled tracks. |
| `SliderRail` | 31 | `0x2A2E35` | Unfilled part of a slider track. |
| `Scrollbar` | 32 | `0x4A5059` | Scrollbar trough and thumb. |
| `ScrollbarHover` | 33 | `0x656C77` | Scrollbar while hovered. |
| `Selection` | 34 | `0xA3E635` @ 0.20 | Text selection highlight. |
| `Separator` | 35 | `0x22252A` | Rules and dividers. |
| `Shadow` | 36 | `0x000000` @ 0.55 | Window and popup shadows. |
| `Glow` | 37 | `0xA3E635` @ 0.35 | Focus glow. |
| `Backdrop` | 38 | `0x000000` @ 0.62 | Modal scrim. |
| `White` | 39 | `1,1,1,1` | Opaque white, for overlays and icon strokes on accent. |
| `Transparent` | 40 | `0,0,0,0` | Fully transparent, used as a "no fill" sentinel. |
| `COUNT` | 41 | | Sentinel. The array size, not a usable colour. |

## StyleVarId

Numeric layout values. Every member is seeded from the active `Theme`'s
`Metrics` and `Motion` by `Style::Reset`. A few have no `Metrics` counterpart and
are seeded from a literal; those are marked.

| Member | Value | Default | Meaning |
|---|---|---|---|
| `Alpha` | 0 | `1.0f` | Multiplier applied to primitive alpha. See the Alpha section. |
| `DisabledAlpha` | 1 | `0.45f` | Intended opacity for a disabled widget. Seeded from a literal. |
| `WindowRadius` | 2 | `14.0f` | Window corner radius. |
| `WindowBorderWidth` | 3 | `1.0f` | Window outline thickness. |
| `WindowPadding` | 4 | `12.0f` | Inset inside the window edge. |
| `WindowShadowSize` | 5 | `28.0f` | Window shadow spread. |
| `FrameHeight` | 6 | `32.0f` | Input frame height. Seeded from `Metrics::fieldHeight`. |
| `FrameRadius` | 7 | `9.0f` | Input frame corner radius. |
| `FrameBorderWidth` | 8 | `1.0f` | Input frame outline thickness. Seeded from a literal. |
| `FramePaddingX` | 9 | `11.0f` | Input frame horizontal padding. |
| `ItemSpacingX` | 10 | `8.0f` | Horizontal gap between items. Seeded from `Metrics::gap`. |
| `ItemSpacingY` | 11 | `5.0f` | Vertical gap between items. Seeded from `Metrics::gapSmall`. |
| `ItemInnerSpacingX` | 12 | `8.0f` | Horizontal gap inside an item. |
| `ItemInnerSpacingY` | 13 | `5.0f` | Vertical gap inside an item. |
| `CellPaddingX` | 14 | `8.0f` | Horizontal padding of a grid cell. |
| `CellPaddingY` | 15 | `8.0f` | Vertical padding of a grid cell. |
| `TitleBarHeight` | 16 | `52.0f` | Title bar height. |
| `SidebarWidth` | 17 | `56.0f` | Sidebar width. |
| `SidebarItemSize` | 18 | `38.0f` | Sidebar icon button size. |
| `SidebarItemRadius` | 19 | `10.0f` | Sidebar icon button radius. |
| `SidebarItemGap` | 20 | `4.0f` | Gap between sidebar items. |
| `ButtonHeight` | 21 | `34.0f` | Button height. |
| `ButtonRadius` | 22 | `9.0f` | Button corner radius. |
| `ButtonBorderWidth` | 23 | `1.0f` | Button outline thickness. Seeded from a literal. |
| `ButtonPaddingX` | 24 | `14.0f` | Button horizontal padding. |
| `SmallButtonHeight` | 25 | `28.0f` | Ghost button height. |
| `IconButtonSize` | 26 | `32.0f` | Square icon button side. |
| `CheckboxSize` | 27 | `20.0f` | Checkbox box size. |
| `CheckboxRadius` | 28 | `6.0f` | Checkbox box corner radius. |
| `ToggleWidth` | 29 | `42.0f` | Toggle track width. |
| `ToggleHeight` | 30 | `23.0f` | Toggle track height. |
| `ToggleRadius` | 31 | `11.5f` | Toggle track corner radius. |
| `ToggleKnobRadius` | 32 | `8.5f` | Toggle knob radius. |
| `SliderHeight` | 33 | `18.0f` | Total slider hit area height. |
| `SliderRailThickness` | 34 | `5.0f` | Slider rail thickness. |
| `SliderThumbRadius` | 35 | `8.0f` | Slider thumb radius. |
| `TabHeight` | 36 | `38.0f` | Tab strip height. |
| `TabPaddingX` | 37 | `16.0f` | Tab horizontal padding. |
| `TabRadius` | 38 | `9.0f` | Tab corner radius. |
| `TabIndicatorThickness` | 39 | `2.5f` | Active tab underline thickness. |
| `RowHeight` | 40 | `34.0f` | List row height. |
| `RowPaddingX` | 41 | `14.0f` | List row horizontal padding. |
| `SeparatorThickness` | 42 | `1.0f` | Separator rule thickness. |
| `SeparatorInset` | 43 | `14.0f` | Separator inset from each side. |
| `SectionRadius` | 44 | `12.0f` | Section corner radius. |
| `SectionHeaderHeight` | 45 | `40.0f` | Section header height. |
| `SectionHeaderPaddingX` | 46 | `14.0f` | Section header horizontal padding. |
| `SectionContentPadding` | 47 | `8.0f` | Padding inside section content. |
| `PopupPadding` | 48 | `6.0f` | Popup inner padding. |
| `PopupRadius` | 49 | `12.0f` | Popup corner radius. |
| `PopupItemHeight` | 50 | `30.0f` | Popup item row height. |
| `PopupItemPaddingX` | 51 | `12.0f` | Popup item horizontal padding. |
| `PopupShadowSize` | 52 | `20.0f` | Popup shadow spread. |
| `ModalRadius` | 53 | `16.0f` | Modal corner radius. |
| `ModalHeaderHeight` | 54 | `56.0f` | Modal header height. |
| `ModalFooterHeight` | 55 | `64.0f` | Modal footer height. |
| `ModalPadding` | 56 | `20.0f` | Modal inner padding. |
| `ModalMinWidth` | 57 | `320.0f` | Minimum modal width. |
| `ModalMinHeight` | 58 | `200.0f` | Minimum modal height. |
| `ListItemHeight` | 59 | `38.0f` | List item height. |
| `ListItemRadius` | 60 | `9.0f` | List item corner radius. |
| `ListIconSize` | 61 | `28.0f` | Leading icon size in a list item. |
| `GridCellRadius` | 62 | `10.0f` | Grid cell corner radius. |
| `GridCellPadding` | 63 | `8.0f` | Grid cell padding. |
| `BadgeHeight` | 64 | `20.0f` | Badge height. |
| `BadgeRadius` | 65 | `6.0f` | Badge corner radius. |
| `BadgePaddingX` | 66 | `8.0f` | Badge horizontal padding. |
| `Gap` | 67 | `8.0f` | General gap. |
| `GapSmall` | 68 | `5.0f` | Small gap. |
| `GapLarge` | 69 | `16.0f` | Large gap. |
| `GapLargeX` | 70 | `16.0f` | Horizontal-only large gap. Seeded from `Metrics::gapLarge`, identical to `GapLarge`. |
| `HoverRate` | 71 | `16.0f` | Hover animation rate. Seeded from `Motion::hoverRate`. |
| `PressRate` | 72 | `28.0f` | Press animation rate. |
| `ExpandRate` | 73 | `14.0f` | Expand animation rate. |
| `ToggleRate` | 74 | `22.0f` | Toggle animation rate. |
| `ModalInRate` | 75 | `15.0f` | Modal fade-in rate. |
| `ModalOutRate` | 76 | `22.0f` | Modal fade-out rate. |
| `ScrollRate` | 77 | `30.0f` | Smooth scroll rate. |
| `TextSize` | 78 | `14.0f` | Body font size. Seeded from `FontManager::kSizeBody`. |
| `TextSizeSmall` | 79 | `12.0f` | Small font size. |
| `TextSizeTiny` | 80 | `11.0f` | Tiny font size. |
| `TextSizeTitle` | 81 | `18.0f` | Title font size. |
| `TextSizeDisplay` | 82 | `24.0f` | Display font size. |
| `COUNT` | 83 | | Sentinel. The array size, not a usable var. |

{% hint style="info" %}
The `COUNT` member of each enum is the array size and is what makes
`Style` able to hold every value in a fixed array. It is never a valid argument
to `GetVar` or `GetColor`; passing it reads past the end of the array.
{% endhint %}

## Style

### `Style::Style`

```cpp
Style();
explicit Style(const Theme& theme);
```

Constructs a style seeded from a theme.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `theme` | `const Theme&` | | Source of the initial colours, metrics and motion values. |

The default constructor uses `Theme::Dark()`. Both go through `Reset`, which
clears the push/pop stacks.

### `Style::Reset`

```cpp
void Reset(const Theme& theme);
```

Re-seeds every value from `theme`, discarding the stack.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `theme` | `const Theme&` | | New source of truth. |

The theme is copied in and is afterwards reachable through `Theme()`. Both
stacks are emptied, so a reset in the middle of a scoped push loses the
unwinding history rather than leaving stale values behind.

### `Style::GetColor`

```cpp
Color GetColor(StyleColorId id) const { return colors_[static_cast<int>(id)]; }
```

**Returns:** the active colour for `id`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleColorId` | | Which colour to read. |

### `Style::GetVar`

```cpp
f32 GetVar(StyleVarId id) const { return vars_[static_cast<int>(id)]; }
```

**Returns:** the active value for `id`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleVarId` | | Which var to read. |

### `Style::SetColor`

```cpp
void SetColor(StyleColorId id, const Color& c);
```

Overwrites a colour permanently. Not affected by the push/pop stack.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleColorId` | | Which colour to write. |
| `c` | `const Color&` | | New value. |

A `SetColor` while a push of the same id is live is still undone by that pop,
which restores the value captured at push time rather than the one you just set.

### `Style::SetVar`

```cpp
void SetVar(StyleVarId id, f32 v);
```

Overwrites a var permanently. Not affected by the push/pop stack.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleVarId` | | Which var to write. |
| `v` | `f32` | | New value. |

### `Style::PushColor`

```cpp
void PushColor(StyleColorId id, const Color& c);
```

Saves the current colour and installs `c`.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleColorId` | | Which colour to override. |
| `c` | `const Color&` | | Value to install. |

Pushes nest, so two pushes on the same id restore in reverse order. Every push
needs a matching pop, counted separately for each push.

### `Style::PushVar`

```cpp
void PushVar(StyleVarId id, f32 v);
```

Saves the current var and installs `v`.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleVarId` | | Which var to override. |
| `v` | `f32` | | Value to install. |

Pushing `StyleVarId::Alpha` is the idiomatic way to fade a scope. See the Alpha
section for which primitives respond.

### `Style::PopColors`

```cpp
void PopColors(int count = 1);
```

Restores up to `count` saved colours, most recent first.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `count` | `int` | `1` | How many entries to unwind. |

Popping more than the stack holds is not an error: it unwinds what is there and
stops. That makes the `default` argument safe to call unconditionally at the end
of a scope that may or may not have pushed.

### `Style::PopVars`

```cpp
void PopVars(int count = 1);
```

Restores up to `count` saved vars, most recent first.

**Returns:** nothing.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `count` | `int` | `1` | How many entries to unwind. |

Like `PopColors`, over-popping unwinds what exists and stops rather than reading
past the bottom of the stack.

### `Style::ColorDepth`

```cpp
int ColorDepth() const { return static_cast<int>(colorStack_.size()); }
```

**Returns:** the number of live colour pushes. Useful in a debug overlay to catch
an unbalanced scope.

### `Style::VarDepth`

```cpp
int VarDepth() const { return static_cast<int>(varStack_.size()); }
```

**Returns:** the number of live var pushes.

### `Style::ClearStack`

```cpp
void ClearStack();
```

Empties both stacks without restoring the saved values.

**Returns:** nothing.

Use it to bail out of a scope without unwinding, for example after a modal
short-circuits a long layout function. The overwritten values stay in place
until the next `Reset`, so pair it with one or with a full `Reset` rather than
expecting a clean frame.

### `Style::Theme`

```cpp
const Theme& Theme() const { return theme_; }
```

**Returns:** a const reference to the theme this style was seeded from and
still holds.

### `Style::MutableTheme`

```cpp
Theme& MutableTheme() { return theme_; }
```

**Returns:** a mutable reference to the held theme.

Editing it changes the seed data, not the live colour and var arrays. The change
takes effect only after a `Reset`, so read-modify-write the palette and then call
`Reset` rather than expecting widgets to see it immediately.

### `Style::Alpha`

```cpp
f32 Alpha() const { return GetVar(StyleVarId::Alpha); }
```

**Returns:** the current style alpha, a multiplier in `[0, 1]` by convention.
`Context::Alpha` forwards to this.

## StyleColorName

```cpp
const char* StyleColorName(StyleColorId id);
```

Name of a style colour, for logs and debug overlays.

**Returns:** the enum member name as a static string, such as `"AccentHover"`,
or `"?"` for any value outside the enum.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `StyleColorId` | | The colour to name. |

Returns `"?"` for `StyleColorId::COUNT` and for anything past the enum; the
string is a literal and stays valid forever.

## See also

- [Theme](theme.md) - the `Palette`, `Metrics` and `Motion` values these seed from.
- [Theming and style](../concepts/theming.md) - how the stack is used in practice.
- [Custom themes](../guides/theming.md) - building a theme of your own.