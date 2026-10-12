# Immediate mode state

An immediate-mode UI redraws every frame, so anything that must survive between
frames needs somewhere to live. ArgonUI keeps it in the context, keyed by a
hashed id path. Your own data stays plain `bool`, `float` and `int`.

## Ids

Every widget derives a stable id from where it sits in the id stack.

```cpp
const u32 id = ctx.GetID("Enabled");   // hash of "Enabled" in the current scope
```

The stack is scoped, so a widget inside a loop needs its own scope:

```cpp
for (int i = 0; i < rows.size(); ++i) {
    ctx.PushID(i);                     // or PushID(i), PushID(ptr), PushID(string)
    RowToggle(rows[i].name, &rows[i].enabled);
    ctx.PopID();
}
```

Without the `PushID` every row would share one identity and fight over one slot
of state. Every `PushID` needs a matching `PopID`.

Ids are a hash, not a pointer, so they are stable across runs as long as the
call order is. Changing the order of widgets in a window can reassign state
between two similar-looking controls. This is the usual immediate-mode caveat and
it is the reason to give widgets distinct labels.

## The state store

```cpp
WidgetState& st = ctx.State(id);
st.value;      // float
st.vec;        // Vec2
st.flag;       // bool
st.payload;    // u32
st.opened;     // bool, used for "is this open"
st.lastBounds; // where the widget was last frame
```

Typed accessors if you prefer:

```cpp
ctx.StateF(id, 0.0f);          ctx.SetStateF(id, v);
ctx.StateB(id, false);         ctx.SetStateB(id, v);
ctx.StateI(id, 0);             ctx.SetStateI(id, v);
ctx.StateV(id);                ctx.SetStateV(id, v);
ctx.HasState(id);
ctx.HoverDuration(id);
```

Widgets in the box use this for their internal state: which tab is active, how
far a section has expanded, whether a combo is open, how far a scroll area has
been flung. You rarely touch it directly unless you are writing a widget.

## Hover, active and focus

Three ids, resolved differently:

- **Hovered** is per frame. A widget claims it when the pointer is inside and
  nothing above it has claimed it first.
- **Active** is sticky. Set on press, cleared on release. It is what makes a
  slider keep tracking after the pointer leaves the rail.
- **Focus** is the keyboard owner, one at a time. Text fields claim it, `Tab`
  and `Escape` release it.

### Hover is resolved by layer

Draw order alone is not enough, because a popup and the widget beneath it both
contain the pointer. ArgonUI adds a layer counter:

```cpp
ctx.PushZ();     // popups, context menus and modals open a higher layer
// ... draw ...
ctx.PopZ();
```

The rule: the first claim on a layer wins, and a higher layer always takes
over. So a modal's backdrop claims hover at its higher layer before the modal's
content draws, and everything underneath it stops being interactive without any
special case in the widget code.

`ItemFlags::AllowOverlap` opts out of the exclusivity rule. The slider rail and
its thumb need it, because they are two items representing one control.

### Input lock

```cpp
ctx.PushInputLock();   // suppresses presses
// ... draw a modal ...
ctx.PopInputLock();
```

A lock stops presses, not hovers. The widget under the cursor still reacts
visually, which is what you want behind a modal's backdrop.

## Popup dismissal, and why it is one frame late

A popup cannot answer "did the user click outside me?" in the frame it opens,
because everything underneath has already processed input by then. Clicking off
a combo would close it and also press the widget behind it.

ArgonUI resolves this across a frame boundary:

```cpp
// while a popup is open, every frame:
ctx.SetPopupCapture(popupRect);
```

`Context::Render` carries that bounds into the next `NewFrame`, where a click
outside it sets `PopupDismissRequested()`. Every widget suppresses its press
that frame, and the popup closes. The net effect is that clicking off a combo
closes it without touching what is underneath.

`PopupList`, `Combo` and `ContextMenu` do this for you. A custom popup needs to
call `SetPopupCapture` with its own rect.

## Animation

Animated values live in the same store, keyed by id.

```cpp
f32 anim = ctx.AnimFloat(id, target ? 1.0f : 0.0f, rate);
f32 t    = ctx.AnimTo(id, target ? 1.0f : 0.0f, duration);
Vec2 v   = ctx.AnimVec2(id, targetVec, rate);
```

`AnimFloat` uses exponential damping, `AnimTo` uses a fixed duration with an
easing curve. Both are continuous and can be swapped mid-flight without a pop,
because the record stores the value at the moment the target last changed.

The helpers in `argonui/Widgets/Internal.h` wrap these for widgets:

```cpp
const f32 hover = detail::HoverAnim(ctx, id, r.hovered, rate);
const f32 press = detail::PressAnim(ctx, id, r.held, rate);
```

Using them for every widget is why hover and press transitions across the whole
UI feel like one product.

## Clipped and self-sizing groups

`BeginSection` records a checkpoint with `DrawCheckpoint()`, draws its body, then
paints the card behind it at `EndSection` by splicing indices into the draw
list. The body height is not declared in advance, which is the whole point. See
[The draw list](draw-list.md).

A closed section clips its body to a zero-height rect rather than skipping it.
There is one layout path instead of two, so you cannot get it wrong by
forgetting to check the return value.

## See also

- [Context reference](../api/context.md)
- [Internal helpers](../api/internal.md)
- [Writing a widget](../guides/writing-a-widget.md)