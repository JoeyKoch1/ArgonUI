# Custom themes

Three levels of theming, from cheapest to most involved.

## Level 1: push colours

Scoped to the widgets between the push and the pop.

```cpp
ctx.PushStyleColor(StyleColorId::Accent, Color::Hex(0xFF6B35u));
ctx.PushStyleColor(StyleColorId::OnAccent, Color::Hex(0xFFFFFFu));

Button("Delete", ButtonKind::Primary);

ctx.PopStyleColor(2);
```

This is the right tool for one-off emphasis. It never touches anything outside
the block, and a missing pop is obvious immediately.

## Level 2: push metrics

```cpp
ctx.PushStyleVar(StyleVarId::ButtonHeight, 44.0f);
ctx.PushStyleVar(StyleVarId::ButtonRadius, 4.0f);   // squarer
ctx.PushStyleVar(StyleVarId::WindowPadding, 20.0f);

// ... widgets ...

ctx.PopStyleVar(3);
```

## Level 3: replace the theme

For a whole look, build a `Theme` and install it.

```cpp
Theme midnight = Theme::Midnight();
midnight.colors.accent = Color::Hex(0x7C5CFFu);
midnight.metrics.windowRadius = 6.0f;
midnight.metrics.buttonRadius = 4.0f;
midnight.motion.hoverRate = 26.0f;
ctx.SetTheme(midnight);
```

`SetTheme` replaces the whole style and clears the push stack, so it is blunt.
Pushes inside the region are the automatic alternative.

## Making a theme from scratch

`Theme` is plain data with no hidden state, so start from empty and fill in
what the widgets need. Every `StyleColorId` and `StyleVarId` has a default that
`Style::Reset` reads out of the theme, so a theme with only a palette already
works.

```cpp
Theme t;
// Palette
t.colors.windowBg     = Color::Hex(0x0A0B0DFFu);
t.colors.windowBorder = Color::Hex(0x1E2126u);
t.colors.cardBg       = Color::Hex(0x131519u);
t.colors.cardBorder   = Color::Hex(0x212429u);
t.colors.sectionHeaderBg = Color::Hex(0x17191DFFu);
t.colors.fieldBg      = Color::Hex(0x1A1D22u);
t.colors.fieldBorder  = Color::Hex(0x272B31u);
t.colors.fieldBorderFocus = t.colors.accent;
t.colors.accent      = Color::Hex(0x00E5FFu);
t.colors.accentHover  = Color::Hex(0x66F0FFu);
t.colors.accentPressed = Color::Hex(0x00B8CCu);
t.colors.accentSoft  = Color::Hex(0x00E5FFu, 0.18f);
t.colors.onAccent     = Color::Hex(0x001318u);
t.colors.text        = Color::Hex(0xE8ECF1u);
t.colors.textDim     = Color::Hex(0x8B94A1u);
t.colors.textMuted   = Color::Hex(0x5A626Eu);
t.colors.track       = Color::Hex(0x2B3038u);
t.colors.sliderRail  = Color::Hex(0x23272Eu);
t.colors.separator    = Color::Hex(0x1B1E23u);
t.colors.shadow      = Color::Hex(0x000000u, 0.60f);
t.colors.glow        = Color::Hex(0x00E5FFu, 0.35f);
t.colors.backdrop    = Color::Hex(0x000000u, 0.70f);

// Metrics
t.metrics.windowRadius  = 6.0f;
t.metrics.buttonRadius  = 4.0f;
t.metrics.toggleRadius  = 11.5f;
t.metrics.sectionRadius = 6.0f;
t.metrics.popupRadius   = 6.0f;
t.metrics.modalRadius   = 8.0f;
t.metrics.gap           = 6.0f;

t.InitVariants();
ctx.SetTheme(t);
```

`InitVariants()` derives the button variants from the palette. Call it last, or
your accent change will not reach the buttons.

Every palette and metrics field is listed in the
[theme reference](../api/theme.md). A field left at its default keeps whatever
`Theme` you copied, so starting from `Theme::Dark()` and changing twenty lines
is usually easier than filling in all two hundred.

## Derived colours

Some colours should follow others rather than being picked independently, or
a retheme looks wrong:

```cpp
t.colors.fieldBorderFocus = t.colors.accent;
t.colors.accentSoft = t.colors.accent.WithAlpha(0.18f);
t.colors.onAccent   = t.colors.accent.Luminance() > 0.5f
                     ? Color::Hex(0x101500u) : Color::Hex(0xFFFFFFu);
```

`Color::LerpC` and `Color::HSV` help build ramps, for a difficulty selector
say:

```cpp
for (int i = 0; i < 8; ++i) {
    difficultyColors[i] = Color::HSV(0.33f - i * 0.033f, 0.75f, 0.85f);
}
```

## Per-user themes

Ship a few and let the user pick. A `Theme` is value data, so storing the
selection as an index is all the persistence you need.

```cpp
enum class ThemeChoice { Dark, Midnight, Light, Custom };

Theme BuildTheme(ThemeChoice c) {
    switch (c) {
    case ThemeChoice::Light:    return Theme::Light();
    case ThemeChoice::Midnight: return Theme::Midnight();
    case ThemeChoice::Custom:   return LoadThemeFromDisk();
    case ThemeChoice::Dark:
    default:                    return Theme::Dark();
    }
}

ctx.SetTheme(BuildTheme(s.themeChoice));
```

## Theming per window

```cpp
void DrawMenu(Context& ctx, MenuState& s) {
    for (int i = 0; i < s.colors.size(); ++i) {
        ctx.PushStyleColor(static_cast<StyleColorId>(i), s.colors[i]);
    }

    BeginWindow(ctx, "main", "Settings", {900, 600});
    // ...
    EndWindow(ctx);

    ctx.PopStyleColor(static_cast<int>(s.colors.size()));
}
```

Pushing the whole palette and popping it after is simpler than saving and
restoring a theme, and it cannot leak.

## Debugging a theme

When something looks wrong, push the colour it uses and try a saturated value:

```cpp
ctx.PushStyleColor(StyleColorId::CardBg, Color::HSV(0.0f, 1.0f, 1.0f));  // red
```

If the offending element turns red, you know which palette key drives it. This
is faster than reading the widget source.

## See also

- [Theming and style](../concepts/theming.md)
- [Theme reference](../api/theme.md)
- [Style reference](../api/style.md)