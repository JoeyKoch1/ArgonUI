# Theme

Every visual decision lives in a `Theme`. A widget never hard-codes a colour, a
radius or a height; it reads them from the active theme. That keeps the
reference look reproducible as a data change rather than a code change, and
makes per-window theming trivial.

A `Theme` is plain data: a `Palette` of colours, a `Metrics` block of numbers, a
`Motion` block of timings, and five `ButtonVariant` records. Copying one is a
plain copy, so per-window theming is a copy plus a few edits.

This page documents [`include/argonui/Theme/Theme.h`](../../include/argonui/Theme/Theme.h).

## Reading a theme through Style

Widgets do not read `Theme` directly. `Style` copies a theme's values into two
fixed arrays and adds a push/pop stack on top, so a widget can be themed for the
duration of a call without mutating the shared theme. See [Style](style.md) for
the var and colour ids a widget actually queries.

The practical consequence for theme authors: editing `Theme::colors.accent`
after the style has been seeded changes nothing until the style is reset.

## The three presets

`Theme::Dark`, `Theme::Light` and `Theme::Midnight` each return a fully
populated theme by value. All three call `InitVariants` so the button variants
match the palette.

| Preset | Accent | Window background | Shadow size | Use it for |
|---|---|---|---|---|
| `Dark` | `0xA3E635` lime | `0x0B0C0E` @ 0.98 | 28 | The default. Games and overlays. |
| `Midnight` | `0xA3E635` lime | `0x07080A` @ 0.97 | 34 | Darker chrome with a deeper shadow, for a floating menu over busy content. |
| `Light` | `0x5B8C00` olive | `0xF7F8FA` @ 0.99 | 22 | A light UI, for a standalone desktop window. |

`Midnight` is derived from `Dark`: it darkens the window, sidebar, title bar and
card surfaces and enlarges the shadow. `Light` replaces the whole palette with
light values and darkens the accent to `0x5B8C00`, because the lime used in the
dark preset has no readable contrast against a white surface. Its disabled text
is notably lighter than the dark preset's disabled text for the same reason.

## Value types

### `StateColors`

An interaction-aware colour set. Widgets pick the entry matching their state
through `For`.

| Field | Type | Meaning |
|---|---|---|
| `normal` | `Color` | Resting colour. |
| `hovered` | `Color` | Pointer is over the item. |
| `active` | `Color` | Item is held or pressed. |
| `disabled` | `Color` | Item is not interactive. |

```cpp
Color For(bool hovered, bool active, bool disabled) const;
```

**Returns:** the colour for the current state.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `hovered` | `bool` | | Pointer is over the item. |
| `active` | `bool` | | Item is held or pressed. |
| `disabled` | `bool` | | Item is not interactive. |

Precedence is `disabled`, then `active`, then `hovered`, then `normal`. A disabled
item ignores its hover state, and an active item is never reported as merely
hovered.

### `ButtonVariant`

Per-visual-variant button styling. One of these describes a whole button rather
than one state colour, because buttons need border and padding decisions too.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `bgNormal` | `Color` | | Resting fill. |
| `bgHovered` | `Color` | | Fill while hovered. |
| `bgActive` | `Color` | | Fill while pressed. |
| `borderNormal` | `Color` | | Outline colour at rest. |
| `borderHovered` | `Color` | | Outline colour while hovered. |
| `fgNormal` | `Color` | | Label colour at rest. |
| `fgHovered` | `Color` | | Label colour while hovered. |
| `fgDisabled` | `Color` | | Label colour when disabled. |
| `borderWidth` | `f32` | `0.0f` | Outline thickness. 0 draws no outline. |
| `radius` | `f32` | `8.0f` | Corner radius. |
| `height` | `f32` | `34.0f` | Button height. |
| `padding` | `Vec2` | `{14.0f, 0.0f}` | Horizontal and vertical inner padding. The Y component is 0 in every preset, so the button is vertically centred by its label. |
| `fontSize` | `f32` | `FontManager::kSizeBody` | Label size, which is 14 by default. |
| `bold` | `bool` | `false` | Whether the label renders in the semibold face. |

There is no `fgActive`: the pressed state changes the fill, not the label. There
is no separate disabled background either, because the widgets push
`StyleVarId::Alpha` when disabled.

`Theme::InitVariants` populates all five variants of a theme. Editing the
palette after that point does not retroactively recolour the buttons.

## Palette

The colour half of a theme. Every field carries the value from `Theme::Dark`.

### Surfaces

| Field | Type | Dark default | Meaning |
|---|---|---|---|
| `windowBg` | `Color` | `0x0B0C0E` @ 0.98 | Main window surface. Slightly transparent so an overlay composites. |
| `windowBorder` | `Color` | `0x23262B` | Window outline. |
| `sidebarBg` | `Color` | `0x101216` | Sidebar surface. |
| `titleBarBg` | `Color` | `0x0E1013` | Title bar surface. |
| `cardBg` | `Color` | `0x16181C` | Card surface. |
| `cardBgHover` | `Color` | `0x1B1E23` | Card surface while hovered. |
| `cardBorder` | `Color` | `0x25282E` | Card outline. |
| `panelBg` | `Color` | `0x111317` | Dropdown and popover surface. |
| `fieldBg` | `Color` | `0x1D2025` | Input field surface. |
| `fieldBgHover` | `Color` | `0x23262C` | Input field surface while hovered. |
| `fieldBorder` | `Color` | `0x2B2F36` | Input field outline. |
| `fieldBorderFocus` | `Color` | `0x3A3F48` | Input field outline while focused. |
| `separator` | `Color` | `0x22252A` | Rules and dividers. |
| `sectionHeaderBg` | `Color` | `0x191C20` | Section header strip. |

### Accent

The lime in the reference UI.

| Field | Type | Dark default | Meaning |
|---|---|---|---|
| `accent` | `Color` | `0xA3E635` | Primary fill, active tab, sidebar marker. |
| `accentHover` | `Color` | `0xB4F04D` | Primary fill while hovered. |
| `accentPressed` | `Color` | `0x8CC72B` | Primary fill while pressed. |
| `accentSoft` | `Color` | `0xA3E635` @ 0.16 | Translucent accent for highlights and the disabled primary label. |
| `onAccent` | `Color` | `0x10150A` | Text drawn on a solid accent fill. Dark, because the accent is light. |

### Text and status

| Field | Type | Dark default | Meaning |
|---|---|---|---|
| `text` | `Color` | `0xF2F4F7` | Body text. |
| `textDim` | `Color` | `0x9AA1AC` | Secondary text and inactive icons. |
| `textMuted` | `Color` | `0x656C77` | Tertiary text and hints. |
| `textDisabled` | `Color` | `0x454B54` | Text of a disabled control. |
| `textOnAccentDark` | `Color` | `0x1B2400` | Text on a *dark* accent, for the light preset's inverted states. |
| `danger` | `Color` | `0xF2555A` | Destructive actions. |
| `dangerSoft` | `Color` | `0xF2555A` @ 0.16 | Translucent danger, hover fill for destructive ghost buttons. |
| `warning` | `Color` | `0xF5B94C` | Warning states. |
| `success` | `Color` | `0x4ADE80` | Success states. |
| `info` | `Color` | `0x60A5FA` | Informational states. |

### Controls

| Field | Type | Dark default | Meaning |
|---|---|---|---|
| `track` | `Color` | `0x33373F` | Slider and toggle track. |
| `trackHover` | `Color` | `0x3D424B` | Track while hovered. |
| `thumb` | `Color` | `0xF2F4F7` | Slider thumb and toggle knob. |
| `thumbAccent` | `Color` | `0xA3E635` | Thumb used on a filled track. |
| `sliderRail` | `Color` | `0x2A2E35` | Unfilled part of a slider track. |
| `scrollbar` | `Color` | `0x4A5059` | Scrollbar trough and thumb. |
| `scrollbarHover` | `Color` | `0x656C77` | Scrollbar while hovered. |
| `selection` | `Color` | `0xA3E635` @ 0.20 | Text selection highlight. |

### Effects

| Field | Type | Dark default | Meaning |
|---|---|---|---|
| `shadow` | `Color` | `0x000000` @ 0.55 | Window and popup shadows. |
| `glow` | `Color` | `0xA3E635` @ 0.35 | Focus glow. |
| `backdrop` | `Color` | `0x000000` @ 0.62 | Modal scrim behind a dialog. |
| `white` | `Color` | `1, 1, 1, 1` | Opaque white. |
| `transparent` | `Color` | `0, 0, 0, 0` | Fully transparent, used as a "no fill" sentinel. |

## Metrics

The numeric half of a theme: sizes, radii, padding and spacing, all in pixels at
100% DPI. Multiply by the DPI scale for a scaled UI.

### Window chrome

| Field | Default | Meaning |
|---|---|---|
| `windowRadius` | `14.0f` | Window corner radius. |
| `windowBorderWidth` | `1.0f` | Window outline thickness. |
| `windowPadding` | `12.0f` | Inset inside the window edge. |
| `windowShadowSize` | `28.0f` | Window shadow spread. |
| `titleBarHeight` | `52.0f` | Title bar height. |
| `sidebarWidth` | `56.0f` | Sidebar width. |
| `sidebarItemSize` | `38.0f` | Sidebar icon button size. |
| `sidebarItemRadius` | `10.0f` | Sidebar icon button radius. |
| `sidebarItemGap` | `4.0f` | Gap between sidebar items. |

### Rows and sections

| Field | Default | Meaning |
|---|---|---|
| `rowHeight` | `34.0f` | List row height. |
| `rowPaddingX` | `14.0f` | List row horizontal padding. |
| `rowGap` | `0.0f` | Gap between list rows. Rows are flush by default. |
| `sectionRadius` | `12.0f` | Section corner radius. |
| `sectionHeaderHeight` | `40.0f` | Section header height. |
| `sectionHeaderPaddingX` | `14.0f` | Section header horizontal padding. |
| `sectionContentPadding` | `8.0f` | Padding inside section content. |
| `separatorThickness` | `1.0f` | Separator rule thickness. |

### Buttons

| Field | Default | Meaning |
|---|---|---|
| `buttonHeight` | `34.0f` | Button height. |
| `buttonRadius` | `9.0f` | Button corner radius. |
| `buttonPaddingX` | `14.0f` | Button horizontal padding. |
| `buttonGap` | `8.0f` | Gap between buttons in a row. |
| `buttonIconGap` | `8.0f` | Gap between a button's icon and its label. |
| `smallButtonHeight` | `28.0f` | Ghost button height. |
| `iconButtonSize` | `32.0f` | Square icon button side. |

### Fields

| Field | Default | Meaning |
|---|---|---|
| `fieldHeight` | `32.0f` | Input field height. |
| `fieldRadius` | `9.0f` | Input field corner radius. |
| `fieldPaddingX` | `11.0f` | Input field horizontal padding. |
| `checkboxRadius` | `6.0f` | Checkbox box corner radius. |
| `checkboxSize` | `20.0f` | Checkbox box size. |

### Toggle

| Field | Default | Meaning |
|---|---|---|
| `toggleWidth` | `42.0f` | Toggle track width. |
| `toggleHeight` | `23.0f` | Toggle track height. |
| `toggleRadius` | `11.5f` | Toggle track corner radius. Half the height makes it a stadium. |
| `toggleKnobRadius` | `8.5f` | Toggle knob radius. |

### Slider

| Field | Default | Meaning |
|---|---|---|
| `sliderHeight` | `18.0f` | Total hit area height, larger than the rail so it is easy to grab. |
| `sliderRailThickness` | `5.0f` | Rail thickness. |
| `sliderThumbRadius` | `8.0f` | Thumb radius. |

### Tabs

| Field | Default | Meaning |
|---|---|---|
| `tabHeight` | `38.0f` | Tab strip height. |
| `tabPaddingX` | `16.0f` | Tab horizontal padding. |
| `tabRadius` | `9.0f` | Tab corner radius. |
| `tabIndicatorThickness` | `2.5f` | Active tab underline thickness. |

### Popups and modals

| Field | Default | Meaning |
|---|---|---|
| `popupPadding` | `6.0f` | Popup inner padding. |
| `popupRadius` | `12.0f` | Popup corner radius. |
| `popupItemHeight` | `30.0f` | Popup item row height. |
| `popupItemPaddingX` | `12.0f` | Popup item horizontal padding. |
| `popupShadowSize` | `20.0f` | Popup shadow spread. |
| `modalRadius` | `16.0f` | Modal corner radius. |
| `modalHeaderHeight` | `56.0f` | Modal header height. |
| `modalFooterHeight` | `64.0f` | Modal footer height. |
| `modalPadding` | `20.0f` | Modal inner padding. |
| `modalMinWidth` | `320.0f` | Minimum modal width. |
| `modalMinHeight` | `200.0f` | Minimum modal height. |

### Grid and list

| Field | Default | Meaning |
|---|---|---|
| `gridCellRadius` | `10.0f` | Grid cell corner radius. |
| `gridCellPadding` | `8.0f` | Grid cell padding. |
| `listItemHeight` | `38.0f` | List item height. |
| `listItemRadius` | `9.0f` | List item corner radius. |
| `listIconSize` | `28.0f` | Leading icon size in a list item. |

### Misc

| Field | Default | Meaning |
|---|---|---|
| `scrollbarWidth` | `5.0f` | Scrollbar thickness. |
| `scrollbarPadding` | `2.0f` | Scrollbar inset from the edge. |
| `gap` | `8.0f` | General gap. |
| `gapSmall` | `5.0f` | Small gap. |
| `gapLarge` | `16.0f` | Large gap. |
| `separatorInset` | `14.0f` | Separator inset from each side. |
| `badgeHeight` | `20.0f` | Badge height. |
| `badgeRadius` | `6.0f` | Badge corner radius. |
| `badgePaddingX` | `8.0f` | Badge horizontal padding. |

{% hint style="info" %}
`rowHeight` (34) and `listItemHeight` (38) are separate numbers that both feed
the row and list look. They are not kept in sync automatically, so a theme that
sets one and not the other will show mixed row heights.
{% endhint %}

## Motion

Animation timings, centralised so the whole UI feels like one product. Two
families live here: rates for the exponential `Damp`, and durations for
fixed-length eased transitions.

### Rates

Higher is snappier. Each feeds a `Damp` call through the matching
`StyleVarId`.

| Field | Default | Feeds | Meaning |
|---|---|---|---|
| `hoverRate` | `16.0f` | `StyleVarId::HoverRate` | Hover fade. |
| `pressRate` | `28.0f` | `StyleVarId::PressRate` | Press-down response. |
| `expandRate` | `14.0f` | `StyleVarId::ExpandRate` | Combo and accordion expand. |
| `toggleRate` | `22.0f` | `StyleVarId::ToggleRate` | Toggle knob travel. |
| `modalInRate` | `15.0f` | `StyleVarId::ModalInRate` | Modal fade and scale in. |
| `modalOutRate` | `22.0f` | `StyleVarId::ModalOutRate` | Modal fade out. Faster than the in-rate so the dialog leaves promptly. |
| `scrollRate` | `30.0f` | `StyleVarId::ScrollRate` | Smooth scroll catch-up. |

### Durations

| Field | Default | Meaning |
|---|---|---|
| `modalInDuration` | `0.16f` | Fixed-length modal entrance, in seconds. |
| `modalOutDuration` | `0.11f` | Fixed-length modal exit, in seconds. |
| `tooltipDelay` | `0.45f` | Delay before a tooltip appears. |
| `tooltipFade` | `0.10f` | Tooltip fade duration. |
| `contextMenuDuration` | `0.12f` | Context menu entrance. |

`hoverRate` through `scrollRate` are also exposed as `StyleVarId` values, so a
`Style` push can retune one interaction without rebuilding a theme. The
durations have no `StyleVarId` equivalent.

{% hint style="warning" %}
A rate is not a duration. `hoverRate = 16` is an exponential approach that
asymptotically reaches its target, so it never quite arrives; `AdvanceAnim` snaps
to the target once within 0.0005. Reach for a duration field when a transition
must finish on a known schedule, and a rate field when it should feel the same
whatever the frame rate.
{% endhint %}

## Theme

```cpp
struct Theme {
    Palette colors;
    Metrics metrics;
    Motion motion;

    ButtonVariant primaryButton{};
    ButtonVariant secondaryButton{};
    ButtonVariant ghostButton{};
    ButtonVariant dangerButton{};
    ButtonVariant iconButton{};
};
```

The whole theme, as plain data with no hidden state. Copying it is safe and
cheap enough to do per window.

### `Theme::Dark`

```cpp
static Theme Dark();
```

The default theme: dark surfaces, lime accent.

**Returns:** a `Theme` by value with `InitVariants` already applied.

### `Theme::Light`

```cpp
static Theme Light();
```

A light theme with an olive accent (`0x5B8C00`), dark status colours and a much
lighter disabled text, because the dark preset's disabled grey disappears on
white.

**Returns:** a `Theme` by value with `InitVariants` already applied.

### `Theme::Midnight`

```cpp
static Theme Midnight();
```

A softer variant used for secondary surfaces. Derived from `Dark` with a darker
window, sidebar, title bar and card, and a 34px shadow for a floating menu.

**Returns:** a `Theme` by value with `InitVariants` already applied.

### `Theme::InitVariants`

```cpp
void InitVariants();
```

Fills the five `ButtonVariant` members from the current palette and metrics.

**Returns:** nothing.

| Preset variant | Fill | Border | Label |
|---|---|---|---|
| `primaryButton` | Accent, then `accentHover`, then `accentPressed` | None | `onAccent`, `fgDisabled` = `accentSoft` |
| `secondaryButton` | `fieldBg`, then `fieldBgHover`, then `cardBgHover` | 1px | `text` |
| `ghostButton` | Transparent until hovered, then `cardBgHover`, then `cardBg` | None | `textDim`, then `text` |
| `dangerButton` | `danger`, then lightened toward white by 12%, then darkened toward `text` by 20% | None | `white` |
| `iconButton` | Transparent until hovered, then `cardBgHover`, then `cardBg` | None | `textDim`, then `text` |

Beyond colour, `InitVariants` also sets the geometry: every variant takes
`metrics.buttonRadius`, and the height comes from the metric that suits it.
`primaryButton`, `secondaryButton` and `dangerButton` use `buttonHeight` (34),
`ghostButton` uses `smallButtonHeight` (28), and `iconButton` uses
`iconButtonSize` (32) with zero padding so it stays square.

{% hint style="info" %}
`InitVariants` sets `borderWidth` but leaves `borderNormal` and `borderHovered`
untouched on all five variants, so those two fields stay at their default black.
The built-in button widget does not read them: `ResolveButton` in `Controls.cpp`
picks the border from the active `Style` (`CardBorder`, lightened 12% toward
white on hover). Set them yourself if you are driving a `ButtonVariant` directly.
{% endhint %}

{% hint style="warning" %}
`InitVariants` reads the palette and metrics and writes the variants. Call it
after you change `colors` or `metrics`, or your new accent will not reach the
buttons. A default-constructed `Theme` has the right field defaults but
uninitialised `ButtonVariant` colours, which is why every preset calls this.
{% endhint %}

**Example**

```cpp
Theme t = Theme::Dark();
t.colors.accent = Color::Hex(0x60A5FAu);   // swap the lime for blue
t.metrics.buttonRadius = 4.0f;              // square off the buttons
t.InitVariants();                           // let the buttons follow
```

## See also

- [Style](style.md) - the mutable view a widget reads, and the push/pop stack.
- [Theming and style](../concepts/theming.md) - how a theme reaches a widget.
- [Custom themes](../guides/theming.md) - building and shipping a theme.
- [Animation](animation.md) - the easing curves `Motion` feeds.