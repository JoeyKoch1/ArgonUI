# Theming and style

Three layers, from most global to most local:

```
Theme          the palette, metrics and motion for a whole look
  Style        a mutable copy with a push/pop stack on top
    widget     reads what it needs, hard-codes nothing
```

A widget never writes a colour literal. That is the property that makes the
reference look a data change rather than a code change.

## Theme

`Theme` is plain data: a `Palette` of colours, a `Metrics` struct of sizes, a
`Motion` struct of animation rates, and a handful of `ButtonVariant`s.

```cpp
ctx.SetTheme(Theme::Dark());      // built in
ctx.SetTheme(Theme::Light());
ctx.SetTheme(Theme::Midnight());
ctx.SetThemePreset("light");      // by name
```

To make your own, start from a preset and change what you want:

```cpp
Theme t = Theme::Dark();
t.colors.accent      = Color::Hex(0xFF6B35u);
t.metrics.windowRadius = 8.0f;
t.motion.hoverRate     = 24.0f;
ctx.SetTheme(t);
```

`InitVariants()` rebuilds the button variants from the palette. Call it after
changing palette colours, which the three presets do internally.

## Style

`Style` is a flat copy of the theme with a push/pop stack.

```cpp
ctx.PushStyleColor(StyleColorId::Accent, Color::Hex(0xFF6B35u));
ctx.PushStyleVar(StyleVarId::ButtonHeight, 44.0f);

Button("Save", ButtonKind::Primary);

ctx.PopStyleVar(1);
ctx.PopStyleColor(1);
```

Pushes are scoped and nest. A push inside a section only affects widgets
between the push and the pop.

Two accessors:

```cpp
Color c = ctx.GetColor(StyleColorId::Accent);
f32   h = ctx.GetVar(StyleVarId::ButtonHeight);
```

## Alpha

```cpp
ctx.PushStyleVar(StyleVarId::Alpha, 0.5f);
// ... widgets ...
ctx.PopStyleVar(1);
```

`Alpha` multiplies through the context primitives (`AddBox`, `AddLine`,
`AddCircle`, `AddImage`) and through text. Widgets that build colours from the
palette and pass them through `ctx.AddBox` or `ctx.AddText` honour it.

The catch: a widget that writes straight to the draw list bypasses it. Custom
widgets should route through the context where possible.

## Theming one widget

Push the colours it reads, draw it, pop.

```cpp
ctx.PushStyleColor(StyleColorId::Accent, Color::Hex(0xFF6B35u));
ctx.PushStyleColor(StyleColorId::OnAccent, Color::Hex(0xFFFFFFu));
Button("Delete", ButtonKind::Primary);
ctx.PopStyleColor(2);
```

## Theming a window

```cpp
void DrawMenu(Context& ctx, MenuState& s) {
    Theme local = Theme::Midnight();
    local.colors.accent = s.accent;
    ctx.SetTheme(local);

    BeginWindow(ctx, "main", "Settings", {900, 600});
    // ...
    EndWindow(ctx);

    ctx.SetTheme(Theme::Dark());   // restore
}
```

`SetTheme` replaces the whole style and clears the stack, so it is a blunt
instrument. Prefer pushes inside a window so the restore is automatic and a
mismatch cannot leak into the rest of the frame.

## Theming a section

Sections read `CardBg`, `CardBorder` and `SectionHeaderBg`.

```cpp
ctx.PushStyleColor(StyleColorId::CardBg, Color::Hex(0x1A1D22u));
BeginSection(ctx, "Advanced", &open);
// ...
EndSection(ctx);
ctx.PopStyleColor(1);
```

## The palette

`Palette` is grouped by role, not by hue, which is what makes a retheme a
lookup rather than an audit.

| Group | Keys |
|---|---|
| Surfaces | `windowBg`, `windowBorder`, `sidebarBg`, `titleBarBg`, `cardBg`, `cardBgHover`, `cardBorder`, `panelBg`, `sectionHeaderBg`, `fieldBg`, `fieldBgHover`, `fieldBorder`, `fieldBorderFocus`, `separator` |
| Accent | `accent`, `accentHover`, `accentPressed`, `accentSoft`, `onAccent` |
| Text | `text`, `textDim`, `textMuted`, `textDisabled`, `textOnAccentDark` |
| Status | `danger`, `dangerSoft`, `warning`, `success`, `info` |
| Controls | `track`, `trackHover`, `thumb`, `thumbAccent`, `sliderRail`, `scrollbar`, `scrollbarHover`, `selection` |
| Effects | `shadow`, `glow`, `backdrop`, `white`, `transparent` |

The dark palette is near-black chrome with a lime accent, which is the look the
reference screenshots show.

## Metrics and motion

`Metrics` holds every size. The ones worth knowing:

- `windowRadius`, `windowBorderWidth`, `windowPadding`, `windowShadowSize`
- `sidebarWidth`, `sidebarItemSize`, `sidebarItemRadius`, `sidebarItemGap`
- `buttonHeight`, `buttonRadius`, `buttonPaddingX`, `buttonBorderWidth`
- `toggleWidth`, `toggleHeight`, `toggleRadius`, `toggleKnobRadius`
- `sliderHeight`, `sliderRailThickness`, `sliderThumbRadius`
- `sectionRadius`, `sectionHeaderHeight`, `sectionContentPadding`
- `popupRadius`, `popupItemHeight`, `modalRadius`, `modalHeaderHeight`

`Motion` holds animation rates, all of which feed the shared helpers in
`Internal.h`. They are per-interaction so the whole UI moves at one speed:

```cpp
t.motion.hoverRate = 16.0f;    // default
t.motion.pressRate = 28.0f;
t.motion.expandRate = 14.0f;  // sections, combos, accordions
t.motion.toggleRate = 22.0f;  // switch knobs
```

## Button variants

```cpp
enum class ButtonKind { Primary, Secondary, Ghost, Danger, Warning };
```

`ResolveButton(kind, style)` turns a kind plus the active style into the full
colour set. It is public, so a custom widget can reuse it:

```cpp
const ButtonColors c = ResolveButton(ButtonKind::Secondary, ctx.GetStyle());
ctx.AddBox(bounds, c.bg, CornerRadius(c.radius), c.borderWidth, c.border);
```

## See also

- [Custom themes](../guides/theming.md)
- [Style reference](../api/style.md)
- [Theme reference](../api/theme.md)