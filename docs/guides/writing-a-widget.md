# Writing a widget

ArgonUI has no widget registration. A widget is a free function that reads the
current context, and that is the whole mechanism.

## The shape of a widget

```cpp
// include/argonui/Widgets/Widgets.h
bool FancyToggle(const char* label, bool* value, bool enabled = true);
```

```cpp
// src/Widgets/Controls.cpp
bool FancyToggle(const char* label, bool* value, bool enabled) {
    Context& ctx = Context::Get();

    const u32 id = ctx.GetID(label);
    WidgetState& st = ctx.State(id);

    const f32 h = ctx.GetVar(StyleVarId::ToggleHeight);
    const f32 w = ctx.GetVar(StyleVarId::ToggleWidth);
    const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(w, h));

    const ItemResult r = ctx.ItemBehavior(
        id, bounds, enabled ? ItemFlags::None : ItemFlags::NoInteraction);

    const bool on = (value != nullptr) && *value;
    const f32 anim = ctx.AnimFloat(id ^ 0x1234u, on ? 1.0f : 0.0f,
                                   ctx.GetVar(StyleVarId::ToggleRate));
    const f32 hover = detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
    const f32 press = detail::PressAnim(ctx, id, r.held, ctx.GetVar(StyleVarId::PressRate));

    const Color track = Color::LerpC(ctx.GetColor(StyleColorId::Track),
                                     ctx.GetColor(StyleColorId::Accent), anim);
    ctx.AddBox(bounds, track, CornerRadius(h * 0.5f));

    const f32 knobR = h * 0.5f - 2.5f;
    const f32 travel = w - knobR * 2.0f - 5.0f;
    const Vec2 kc(bounds.min.x + 2.5f + knobR + travel * anim, bounds.GetCenter().y);
    ctx.AddBox(Rect(kc.x - knobR, kc.y - knobR, kc.x + knobR, kc.y + knobR),
               ctx.GetColor(StyleColorId::Thumb), CornerRadius(knobR));

    ctx.ItemSize(bounds.Size());
    ctx.NewLine();

    if (r.clicked && enabled && value != nullptr) *value = !*value;
    return r.clicked && enabled;
}
```

Five steps, in this order:

1. `GetID` for identity
2. Read the rect from the layout cursor
3. `ItemBehavior` for hover, press, click
4. Draw with `ctx.Add*`
5. `ItemSize` then `NewLine`

## Rules

**Never hard-code a colour or a size.** Read them:

```cpp
ctx.GetColor(StyleColorId::Accent)
ctx.GetVar(StyleVarId::ButtonRadius)
```

A widget that writes `Color(0.65f, 0.9f, 0.2f)` cannot be themed, and that is
the single most common way a custom widget breaks a theme.

**Always use the shared animation helpers.**

```cpp
detail::HoverAnim(ctx, id, r.hovered, ctx.GetVar(StyleVarId::HoverRate));
detail::PressAnim(ctx, id, r.held,   ctx.GetVar(StyleVarId::PressRate));
detail::ExpandAnim(ctx, id, open);
```

Using them for every widget is why hover feels identical across the UI. A
custom rate makes one control feel wrong next to its neighbours.

**Always route drawing through the context.** `ctx.AddBox` applies the style
alpha; `drawList().AddBox` does not.

**Finish with `ItemSize` and `NewLine`.** Otherwise the next widget draws on top
of this one.

**Never change the cursor unless you mean to.** Widgets that need to place
something inline call `SetCursorPos` and the caller is expected to restore it.
`Row` does this for you. See below.

## Id hygiene

```cpp
const u32 id = ctx.GetID(label);
```

The label is the identity. Two widgets with the same label in the same id scope
share state, which shows up as one of them flickering when the other is used.

For widgets with several internal parts, derive sub-ids by mixing:

```cpp
const u32 id    = ctx.GetID("FancyToggle");
const u32 animI = id ^ 0x1234u;
const u32 hitI  = ctx.GetID("hit", "FancyToggle");
```

Deriving by XOR keeps the parts distinct and still stable across frames.

## Drawing inline into a row

`Row` hands your callback a rect and saves the cursor around the call, so the
outer layout is unaffected:

```cpp
struct Ref { bool* v; };
bool DrawMyControl(Context& ctx, const Rect& r, void* ud) {
    auto* ref = static_cast<Ref*>(ud);
    ctx.SetCursorPos(Vec2(r.max.x - 40.0f, r.GetCenter().y - 20.0f));
    return FancyToggle("", ref->v);
}

bool RowFancy(const char* label, bool* v) {
    Context& ctx = Context::Get();
    RowOptions opt;
    Ref ref{v};
    Row(ctx, label, opt, DrawMyControl, &ref);
    return ref.changed;
}
```

`Row` returns whether the control changed. Capture it in your ref struct rather
than trying to propagate it out of the callback.

## Self-sizing groups

If your widget contains a variable amount of content and needs a background
behind it:

```cpp
bool BeginCard(Context& ctx, const char* title, f32* heightOut) {
    const u32 checkpoint = ctx.DrawCheckpoint();
    ctx.PushID(title);
    const Rect top = ctx.ContentRegionAvail();
    ctx.BeginLayout(Rect(top.min.x, top.min.y, top.max.x, 0.0f));
    // ... caller draws content ...
    return true;
}

void EndCard(Context& ctx, f32 height) {
    ctx.EndLayout();
    ctx.PopID();
    ctx.MutableDrawList().InsertBackgroundBox(
        card, CornerRadius(12.0f), ctx.GetColor(StyleColorId::CardBg), 1.0f,
        ctx.GetColor(StyleColorId::CardBorder), 0.0f, Color(0, 0, 0, 0),
        checkpoint, clipAtCheckpoint);
}
```

See [Immediate mode state](../concepts/state.md) for the mechanism.

## Picking an icon

```cpp
DrawIcon(Icon::Crosshair, rect, 1.7f, ctx.GetColor(StyleColorId::Accent));
```

Thickness in pixels, colour from the style, so it follows hover like anything
else. Around 60 icons ship in the box and adding one is one enum entry and one
`case` in `Icons.cpp`.

## Testing a widget

Drive it headlessly:

```cpp
auto* backend = argon::ui::CreateBackend(GraphicsApi::Null);
Context ctx;
ContextConfig cfg;
cfg.submitAutomatically = false;
ctx.Initialize(backend);

FrameState fs;
fs.displaySize = { 1280.0f, 800.0f };
fs.deltaTime = 1.0f / 60.0f;

// simulate a click in the middle of the widget
ctx.NewFrame(fs);
ctx.Input().mousePos = { 640.0f, 400.0f };
ctx.Input().mouseDown[Mouse_Left] = true;
ctx.BeginLayout(Rect(0, 0, 1280, 800));
bool clicked = FancyToggle("ESP", &value);
ctx.EndLayout();

assert(clicked);
assert(value);
```

The null backend renders nothing and records stats, so a widget can be regression
tested without a display.

## Checklist

- [ ] Colours and sizes from `ctx.Color` and `ctx.Var`
- [ ] Animation through the `detail::` helpers
- [ ] Drawing through `ctx.Add*`
- [ ] `ItemSize` then `NewLine`
- [ ] `ItemFlags::NoInteraction` when disabled
- [ ] Id derived from the label, sub-ids XORed
- [ ] Returns whether it changed
- [ ] Adds a `## See also` entry to the API page

## See also

- [Controls reference](../api/controls.md)
- [Internal helpers](../api/internal.md)
- [Icons](../api/icons.md)