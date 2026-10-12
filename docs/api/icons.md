# Icons

The stroke icon set. There is no icon font, no SVG rasterizer and no texture
atlas: every glyph is a few lines, arcs and rings drawn with the same
primitives as the rest of the UI, so an icon batches with the widgets around it
and inherits their hover state.

Header: [`include/argonui/Widgets/Icons.h`](../../include/argonui/Widgets/Icons.h).

## The drawing model

Artwork is authored on a 24 by 24 design grid with the origin at the top-left
and Y growing downward, exactly like SVG. Each glyph is a `switch` case built
from a small `Pen` helper whose methods take grid coordinates and translate
them:

| Pen method | Draw list primitive | Shape |
|---|---|---|
| `Line` | `DrawList::AddLine` | A straight capsule with round caps, at any angle. |
| `Box` | `DrawList::AddBox` | A filled rectangle with an optional corner radius. |
| `Circle` | `DrawList::AddCircle` | A circle inscribed in a square. |
| `Arc` | `DrawList::AddArc` | A ring segment. Angles are degrees clockwise from 3 o'clock. |
| `Tri` | `DrawList::AddTriangle` | A triangle. Defined for new artwork; no current glyph uses it. |
| `Poly` | `DrawList::AddLine` in a loop | A closed polyline from a flat coordinate array. Used by `Tag`, `Cube`, `Layers` and `Sparkle`. |
| `Star` | `DrawList::AddLine` in a loop | A five-pointed star, drawn as ten connected spokes. |

Four consequences fall out of that model, and they are the reason the set looks
consistent:

- **Color is applied at draw time, not baked in.** A glyph carries no color of
  its own. `DrawIcon` multiplies the color you pass by the alpha you pass and
  uses the result for every primitive. Passing the same color a widget uses for
  its label, or an animated blend of its resting and hover colors, makes the
  icon animate with the widget for free.
- **Stroke weight is a parameter, not a constant.** The same case renders a
  hairline divider or a heavy emblem depending on `thickness`. Widgets pass a
  fraction of their own size, so an icon scales with the control around it.
- **It is resolution independent.** Everything is geometry, so it stays crisp at
  any DPI and needs no atlas rebuild when the scale factor changes.
- **It is free to batch.** Icons emit the same quads as `Context::AddBox` and
  `Context::AddLine`, so consecutive icons merge into the same draw call as the
  rest of the frame.

Adding a glyph means appending to the `Icon` enum and adding one `case` to
`Icons.cpp`. Keep the new artwork on the same grid and the same optical size as
its neighbors.

## The set

### `Icon`

```cpp
enum class Icon : u16 {
    None = 0,

    // Navigation / chrome
    Search, Close, Check, Minus, Plus,
    ChevronUp, ChevronDown, ChevronLeft, ChevronRight,
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
    Menu, More, DotsHorizontal, DotsVertical,
    ExternalLink,

    // Files
    Folder, FolderOpen, File, Save, Download, Upload, Image, Trash, Copy, Clipboard,

    // Toggles & settings
    Settings, Sliders, Eye, EyeOff, Lock, Unlock, Bell, BellOff,
    Power, Refresh, Rotate, Link, Unlink,

    // Domain
    User, Users, Tag, Bolt, Cube, Layers, Target, Crosshair, Shield, Star, Heart,
    Home, Grid, List, Palette, Sparkle, Info, Warning, Error, CheckCircle,
    Play, Pause, Stop, Expand, Collapse, Filter, SortAscending, SortDescending,

    COUNT
};
```

69 glyphs plus `None` and the `COUNT` sentinel. `None` and `COUNT` both fall
through to a drawing case that emits nothing, so they are safe to pass
anywhere.

**Navigation and chrome** (18): `Search`, `Close`, `Check`, `Minus`, `Plus`,
`ChevronUp`, `ChevronDown`, `ChevronLeft`, `ChevronRight`, `ArrowLeft`,
`ArrowRight`, `ArrowUp`, `ArrowDown`, `Menu`, `More`, `DotsHorizontal`,
`DotsVertical`, `ExternalLink`.

**Files** (10): `Folder`, `FolderOpen`, `File`, `Save`, `Download`, `Upload`,
`Image`, `Trash`, `Copy`, `Clipboard`.

**Toggles and settings** (13): `Settings`, `Sliders`, `Eye`, `EyeOff`, `Lock`,
`Unlock`, `Bell`, `BellOff`, `Power`, `Refresh`, `Rotate`, `Link`, `Unlink`.

**Domain** (28): `User`, `Users`, `Tag`, `Bolt`, `Cube`, `Layers`, `Target`,
`Crosshair`, `Shield`, `Star`, `Heart`, `Home`, `Grid`, `List`, `Palette`,
`Sparkle`, `Info`, `Warning`, `Error`, `CheckCircle`, `Play`, `Pause`, `Stop`,
`Expand`, `Collapse`, `Filter`, `SortAscending`, `SortDescending`.

{% hint style="info" %}
`More` and `DotsVertical` share one drawing case and `Refresh` and `Rotate`
share another, so 69 values produce 67 distinct drawings. They are separate
values because the surrounding widget may want a different name for the same
shape, not because the shape differs.
{% endhint %}

**Returns:** nothing, it is an enumeration.

## Drawing

### `DrawIcon`

```cpp
void DrawIcon(DrawList& list, Icon icon, const Rect& box, f32 thickness, const Color& color,
              f32 alpha = 1.0f);
```

Draws `icon` inside `box`. The 24-unit grid is scaled to fit the smaller side
of the box with aspect preserved and centered inside it, so the drawing never
exceeds the box in either axis. The stroke width is clamped to at least 0.75
pixels so a thin icon at a small size does not disappear entirely.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `list` | `DrawList&` | | Draw list to append to. Usually `Context::MutableDrawList()`. |
| `icon` | `Icon` | | Glyph to draw. `Icon::None` and `Icon::COUNT` emit nothing. |
| `box` | `const Rect&` | | Destination rectangle in pixels. An empty rect emits nothing. |
| `thickness` | `f32` | | Stroke width in pixels, floored at 0.75. |
| `color` | `const Color&` | | Stroke and fill color, tinted at draw time. |
| `alpha` | `f32` | `1.0f` | Multiplied into `color.a`. |

**Returns:** nothing, it appends to the draw list.

{% hint style="warning" %}
The glyph is inset by 16 percent of the box on every side, so the artwork
occupies the middle 68 percent. A `box` sized to the artwork makes the icon look
smaller than the surrounding text; size the box from the widget height and a
fraction of it, the way the built-in widgets do.
{% endhint %}

### `DrawIcon`

```cpp
void DrawIcon(Icon icon, const Rect& box, f32 thickness, const Color& color, f32 alpha = 1.0f);
```

The convenience overload for the current context. Identical behavior, with the
draw list taken from `Context::Get()`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `icon` | `Icon` | | Glyph to draw. |
| `box` | `const Rect&` | | Destination rectangle in pixels. |
| `thickness` | `f32` | | Stroke width in pixels, floored at 0.75. |
| `color` | `const Color&` | | Stroke and fill color. |
| `alpha` | `f32` | `1.0f` | Multiplied into `color.a`. |

**Returns:** nothing.

**Example**

```cpp
using namespace argon::ui;

const u32 id = ctx.GetID("menu.accent");
const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(24.0f, 24.0f));
const ItemResult r = ctx.ItemBehavior(id, bounds);

// Tint at draw time, so the glyph animates with the hover state of the item
// instead of carrying a color of its own.
const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.Var(StyleVarId::HoverRate));
const Color tint = Color::LerpC(ctx.Color(StyleColorId::TextDim),
                                ctx.Color(StyleColorId::Accent), hover);

DrawIcon(Icon::Sparkle, IconBox(bounds.GetCenter(), 20.0f), 1.7f, tint);
ctx.ItemSize(bounds.Size());
ctx.NewLine();
```

### `IconBox`

```cpp
inline Rect IconBox(const Vec2& center, f32 size);
```

A square box of side `size` centered on `center`. This is the rect every built-in
widget passes to `DrawIcon`, and using it keeps hand-placed icons optically
centered.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `center` | `const Vec2&` | | Center point in pixels. |
| `size` | `f32` | | Side length in pixels. |

**Returns:** the square rectangle, as a `Rect` constructed directly from its
edges.

### `IconMetrics`

```cpp
struct IconMetrics {
    f32 inset = 0.14f;   ///< Fraction of the box kept as padding on each side.
};
```

The padding a glyph keeps inside its box, as a fraction of the box side. It
describes the optical grid the set is drawn on.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `inset` | `f32` | `0.14f` | Padding on each side as a fraction of the box side. |

**Returns:** nothing, it is a plain data struct.

{% hint style="warning" %}
`DrawIcon` does not read `IconMetrics`. It uses a hard-coded inset of 0.16, so
changing this value has no effect on what is rendered. Treat it as
documentation of the optical grid until the implementation reads it.
{% endhint %}

## See also

- [Controls](controls.md)
- [Containers, pickers and overlays](widgets.md)
- [Internal helpers](internal.md)
