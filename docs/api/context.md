# Context

`Context` is the single object an application talks to. It owns the frame's draw
list, the input snapshot, the id stack, the layout cursor, the style stack, the
persistent widget state map, the animation store and the font family. Every
widget in the library is a free function that operates on the current context,
and every one of them also has an overload taking an explicit `Context&` so two
contexts can coexist on one thread.

Read this page if you are integrating the library, writing a widget, or
debugging hover, focus or layout.

Source: [`include/argonui/Context.h`](../../include/argonui/Context.h),
implementation in [`src/Context.cpp`](../../src/Context.cpp).

```cpp
Context ctx;
ctx.Initialize(backend, config);

while (running) {
    ctx.Input().mousePos = pointer();
    for (int b = 0; b < Mouse_COUNT; ++b) ctx.Input().mouseDown[b] = button(b);

    FrameState fs;
    fs.deltaTime = dt;
    fs.displaySize = size;
    ctx.NewFrame(fs);
    ctx.BeginLayout(Rect(0, 0, size.x, size.y));
    DrawYourMenu(ctx);
    ctx.EndLayout();
    ctx.Render();
}
```

## Lifecycle

Three static members decide which context everything else talks to. `Current`
reads a thread-local pointer, `SetCurrent` writes it, and `Get` turns it into a
reference. Every widget free function starts with `Context& ctx = Context::Get();`,
so an application that never calls `SetCurrent` works as long as exactly one
`Initialize` has run.

`Get` never crashes the host. When no context is current it returns a
function-local static that was default-constructed and never initialized: no
backend, no frame state, a one-pixel root layout. Every accessor on it is safe,
so a stray widget call made from a thread that never set a current context is a
no-op rather than a fault.

```cpp
static Context* Current();
static void SetCurrent(Context* ctx);
static Context& Get();
```

| Function | Returns |
|---|---|
| `Current()` | The thread-local current context, or null. |
| `SetCurrent(ctx)` | Nothing. A null argument clears it. |
| `Get()` | A reference to the current context, or to an inert fallback. |

{% hint style="info" %}
The fallback is silent, not visible. A widget that draws into it appends to a
draw list nobody will submit, because `Render` returns early when there is no
backend. A missing context shows up as a blank UI, so check `Current()` once at
startup rather than debugging it per frame.
{% endhint %}

The thread-local matters as soon as there is more than one thread. A render
thread that draws an in-game HUD needs its own `SetCurrent`, and a worker thread
with no business touching the UI should never call `Get`.

### `Context::Context` and `Context::~Context`

```cpp
Context();
~Context();
Context(const Context&) = delete;
Context& operator=(const Context&) = delete;
```

**Returns:** nothing, both are lifetime hooks.

The constructor reserves the id, layout and clip stacks, reserves the state and
animation maps, reserves a 16384-vertex draw list and pushes a 1x1 root layout.
None of it needs a backend, so a context is safe to construct before the graphics
device exists.

The destructor calls `Shutdown`, so an explicit call is optional. The copy
operations are deleted: the id stack, the layout stack and the state map are
meaningless when two contexts share them, and a silent copy would produce two
objects writing the same ids.

### `Context::Initialize`

```cpp
bool Initialize(IArgonBackend* backend, const ContextConfig& config = {});
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `backend` | `IArgonBackend*` | | Already-initialized backend used for textures and submission. |
| `config` | `const ContextConfig&` | `{}` | Defaults for dpi, font directory, capacities and diagnostics. |

**Returns:** always true. The only failure mode is font loading, which logs
`ARGON_WARN("Context", "font initialisation failed; text will be invisible")`
and carries on.

Initializes the font family from `config.fontFamilyDir` and `config.dpiScale`,
points `CurrentFont` at the body-size regular face, reserves the draw list to
`config.vertexCapacity` and `config.indexCapacity`, resets the style stack and
rebuilds the root layout with the current item spacing.

The graphics API is not a parameter. It belongs to the backend, which was
created by `CreateBackend(GraphicsApi)` before it got here. There is no
`Initialize` overload that takes a `GraphicsApi`.

```cpp
ContextConfig config;
config.dpiScale = 1.5f;
config.fontFamilyDir = "C:/Assets/Fonts";
config.submitAutomatically = false;   // the host presents, not us
ctx.Initialize(backend, config);
```

{% hint style="warning" %}
The backend must outlive the context, or be handed over with `SetBackend` first.
`Shutdown` destroys the font atlases and every texture the context registered
through the backend, so calling it against a backend that has already been torn
down reaches into freed device objects.
{% endhint %}

### `Context::Shutdown`

```cpp
void Shutdown();
```

**Returns:** nothing. It releases GPU resources and clears the backend pointer.

Releases the font atlas textures and every texture obtained from
`RegisterTexture` through the backend, then clears the owned texture list. The
destructor calls it, so an explicit call is optional but harmless, and the demo
calls it before returning so device destruction order is obvious.

### `Context::SetBackend`

```cpp
void SetBackend(IArgonBackend* backend);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `backend` | `IArgonBackend*` | | New backend. Null detaches. |

**Returns:** nothing.

Swaps the backend. When the pointer actually changes, the old one is used first
to destroy the font atlases and the textures the context registered, so a device
swap does not leak. Passing the same pointer again is a no-op beyond the
assignment, which makes it safe to call every frame.

{% hint style="warning" %}
Despite the name, this does not take ownership in the current implementation.
The flag that would make `Shutdown` delete the backend is never set by any
public method, so the context never deletes an `IArgonBackend`. The caller keeps
that responsibility and must `delete` it itself.
{% endhint %}

### `Context::Backend`

```cpp
IArgonBackend* Backend() const { return backend_; }
```

**Returns:** the backend currently attached, or null before `Initialize`.

Useful for reading `BackendInfo` or for an overlay that wants to know whether it
is driving its own swapchain or an adopted one.

## The frame

`NewFrame` resets everything that is per frame and derives the input edges.
`Render` finalizes the draw list and, when the context is configured to submit
automatically and a backend exists, pushes it through
`BeginFrame` / `Render` / `Present` / `EndFrame`.

### `Context::NewFrame`

```cpp
void NewFrame(const FrameState& state);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `state` | `const FrameState&` | | Frame index, delta time, time, display size, dpi scale, focus and occlusion. |

**Returns:** nothing. It must be the first call of every frame.

In order, it:

1. Copies `state` and clamps `deltaTime` to the 250 ms maximum.
2. Resets the draw list.
3. Derives the input edges from the `mouseDown` latches.
4. Advances the `timer` on every live widget state.
5. Resolves a pending popup dismissal.
6. Clears `hoveredId`, `hoverWinnerZ`, `wantsFocus`, `lastItem`, the z stack and
   the input lock counter.
7. Rebuilds the root layout as `Rect(0, 0, displaySize.x, displaySize.y)`.
8. Clears the style stack.
9. Pushes the frame's dpi scale into the font manager and re-uploads any dirty
   atlas.

{% hint style="info" %}
The delta time clamp is a safety net, not a frame limiter. A hitch, a debugger
break or a suspended process produces one enormous delta, and an animation that
integrates it in a single step jumps straight to its target instead of
animating. A negative or zero delta is replaced by the same 250 ms, so an
uninitialized `FrameState` cannot freeze every animation in the process.
{% endhint %}

### `Context::Render`

```cpp
void Render();
```

**Returns:** nothing. It finalizes the draw list and submits it.

Publishes the popup bounds collected this frame so the next `NewFrame` can test
for an outside click, then calls `DrawList::Finalize`. When
`ContextConfig::showMetrics` is set or the metrics overlay was switched on, it
draws that overlay and finalizes a second time, because the overlay added
geometry after the first merge pass.

Submission then runs only when a backend is attached and
`config_.submitAutomatically` is true. In that case it calls
`backend_->BeginFrame(frame_)`, and only when that returns true
`backend_->Render(drawList_, frame_)`, `Present()` and `EndFrame()`.

{% hint style="warning" %}
When `BeginFrame` returns false the function returns without calling
`EndFrame()`. That is the intended behaviour for an occluded or device-lost
frame, but it means a backend whose per-frame allocations need releasing on the
skip path will not get the call.
{% endhint %}

### `Context::GetDrawList`

```cpp
const DrawList& GetDrawList() const { return drawList_; }
```

**Returns:** a const reference to the frame's draw list.

Valid between `NewFrame` and `Render`. Read it to assert on a frame, to count
batches, or to hand the geometry to a renderer of your own. After `Render` the
backend has consumed it and the next `NewFrame` empties it.

### `Context::MutableDrawList`

```cpp
DrawList& MutableDrawList() { return drawList_; }
```

**Returns:** a mutable reference to the frame's draw list.

Needed for the two things a widget cannot do through `Context`: `InsertBackgroundBox`
and the `DrawIcon` family, which writes straight into the vertex stream. Going
through this reference bypasses the context's style alpha, so a widget that
draws through it must apply its own opacity.

### `Context::DrawCheckpoint`

```cpp
u32 DrawCheckpoint() const { return drawList_.IndexCheckpoint(); }
```

**Returns:** the current index count, as a value to hand back to
`DrawList::InsertBackgroundBox`.

Capture it in a `Begin` function and use it in the matching `End` function to
paint a container's background behind content whose height is only known at the
end. The clip rect passed to `InsertBackgroundBox` must be the one that was
active at the checkpoint, not the one active at the end, which is why the
containers capture `ClipRect()` alongside the checkpoint.

### `Context::Frame`

```cpp
const FrameState& Frame() const { return frame_; }
```

**Returns:** the frame state as it was after `NewFrame`, including the clamped
delta time.

### `Context::DeltaTime`

```cpp
f32 DeltaTime() const { return frame_.deltaTime; }
```

**Returns:** seconds since the previous frame, clamped to 0.25. The unit every
animation rate is expressed against.

### `Context::Time`

```cpp
f32 Time() const { return frame_.time; }
```

**Returns:** seconds since the host started accumulating. It is the host's
`FrameState::time`, not a clock the context keeps, so it must advance
monotonically or every duration and double-click window misbehaves.

### `Context::FrameIndex`

```cpp
u32 FrameIndex() const { return frame_.frameIndex; }
```

**Returns:** whatever the host put in `FrameState::frameIndex`. The context does
not increment it.

## Input

### `Context::Input`

```cpp
InputState& Input() { return input_; }
const InputState& Input() const { return input_; }
```

**Returns:** the frame's input snapshot, mutable or const.

This is where the host writes. It holds `mousePos`, `mouseDown[]`, `mouseWheel`,
`keyDown[]`, `modifiers` and `inputCodepoints` as inputs, and everything else as
outputs that `NewFrame` derived. See [Input](input.md) for the field by field
description.

```cpp
ctx.Input().mousePos = cursor;
ctx.Input().mouseWheel = wheelDelta;
for (int b = 0; b < Mouse_COUNT; ++b) {
    ctx.Input().mouseDown[b] = (buttons & (1u << b)) != 0;
}
ctx.Input().modifiers = mods;
for (u32 cp : typedThisFrame) ctx.Input().inputCodepoints.push_back(cp);
```

{% hint style="warning" %}
Write only the latches. `mouseClicked`, `mouseReleased`, `mouseDoubleClicked`,
`clickPos`, `releasePos` and `mouseDownDuration` are derived by `NewFrame`, and
`mousePrevPos` and `mouseDelta` are overwritten by it, so writing them yourself
either fights the derivation or is silently discarded. A host that reports
"button is down" rather than "button went down this frame" is a host that works
on every platform.
{% endhint %}

The keyboard is the one place where the contract is looser than the mouse.
`NewFrame` derives the mouse edges from the `down` latches, but the keyboard
edges are the host's to fill. Clear them each frame with
`InputState::ClearFrameEdges` or a key press will repeat forever.

### `Context::SetInput`

```cpp
void SetInput(const InputState& state) { input_ = state; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `state` | `const InputState&` | | Snapshot to copy in, replacing the current one. |

**Returns:** nothing, it assigns a whole struct.

Useful when the platform layer already produces an `InputState` of its own and
you would rather not re-expose every field through `Input()`. It copies the
derived fields too, so call it before `NewFrame` or the derivation overwrites
what you copied.

### `Context::IsMouseHoveringRect`

```cpp
bool IsMouseHoveringRect(const Rect& r) const;
bool IsMouseHoveringRect(const Rect& r, bool clipped) const;
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Rectangle to test, in the same space as `Input().mousePos`. |
| `clipped` | `bool` | `true` | Intersect with the current clip rect first. |

**Returns:** true when the pointer is inside `r`. An empty rectangle is always
false. `Rect::Contains` is half open, so a point on the right or bottom edge is
outside.

With `clipped` true the rect is intersected with `ClipRect()` and a zero-area
result short-circuits to false. That is the correct test for anything inside a
scrolling region, because a row scrolled out of view is not under the pointer
even though its coordinates still are.

### `Context::IsAnyMouseClicked`

```cpp
bool IsAnyMouseClicked() const;
```

**Returns:** true when any of the five buttons went down this frame.

A global "did the user click anywhere" test, used by a context menu to decide
whether it should open. It says nothing about where.

### `Context::IsMouseDragging`

```cpp
bool IsMouseDragging() const { return dragDistance_ > dragThreshold_; }
bool IsMouseDragging(MouseButton b) const {
    return activeMouseButton_ == b && IsMouseDragging();
}
```

**Returns:** true when the pointer has moved further than the drag threshold
since the press. The overload also requires `b` to be the button that started
the drag.

`dragDistance_` is the straight-line distance from the press point, recomputed
each frame while something owns the pointer. It is zero on the frame the button
went down and starts accumulating once an item has taken the active id.

### `Context::MouseDragThreshold`

```cpp
f32 MouseDragThreshold() const { return dragThreshold_; }
```

**Returns:** the distance in pixels a drag must exceed, 4.0.

There is no setter, so every widget in the library uses the same threshold. That
is a deliberate choice: a slider that started dragging at a different distance
from a scroll area feels broken.

### `Context::WantsCaptureMouse`

```cpp
bool WantsCaptureMouse() const;
```

**Returns:** true when an item is hovered or holds the active id.

The test an overlay uses to decide whether to swallow the click rather than
forwarding it to the game underneath. The implementation is
`hoveredId_ != 0 || activeId_ != 0`; it does not know about popups, so a click
over an open combo also reports capture. Pair it with `PopupWasOpen` when the
difference matters.

### `Context::WantsCaptureKeyboard`

```cpp
bool WantsCaptureKeyboard() const;
```

**Returns:** true when a focus id is set, which in practice means a text field
owns the keyboard.

A text field claims focus by calling `SetFocusId`, so this is the answer to "is
a text input currently typing?".

## Ids and focus

Every widget's identity is a hash of its label plus the path of `PushID` scopes
around it. That path is what makes two buttons both labelled "Delete" in
different windows independent, and it is also why wrapping a widget in `PushID`
changes its stored state: a different path is a different id, a different slot
in the state map, and the old value is left behind.

### `Context::PushID`

```cpp
void PushID(const char* id);
void PushID(const std::string& id);
void PushID(int id);
void PushID(const void* ptr);
```

| Overload | Parameter | Meaning |
|---|---|---|
| `const char*` | `id` | Hashes the string into the current scope. |
| `const std::string&` | `id` | Forwards to the `const char*` overload. |
| `int` | `id` | Mixes the integer with the golden-ratio constant, then the scope. |
| `const void*` | `ptr` | Mixes the low 32 bits of the pointer, then the scope. |

**Returns:** nothing, it pushes one level onto the id stack.

An empty stack seeds with the FNV-1a offset basis `0x811C9DC5`. The string form
runs FNV-1a over the bytes; the integer and pointer forms mix with `0x9E3779B9`.
Every path returns 1 rather than 0, because 0 means "no id" everywhere else in
the API.

{% hint style="warning" %}
Balance every `PushID` with a `PopID`. The pop is guarded, so an unbalanced
push does not crash, it leaks: every id created afterwards carries the extra
scope segment, and its state, animation and focus entries are stranded under
ids that nothing will ever ask for again.
{% endhint %}

```cpp
for (int i = 0; i < rows.size(); ++i) {
    ctx.PushID(i);
    if (ctx.GetID("row") == idOfThisRow) { /* ... */ }
    ctx.PopID();
}
```

### `Context::PopID`

```cpp
void PopID();
```

**Returns:** nothing. Pops one scope. A pop on an empty stack does nothing.

### `Context::IDStackDepth`

```cpp
int IDStackDepth() const { return static_cast<int>(idStack_.size()); }
```

**Returns:** how many scopes are currently pushed.

Useful as an assertion in a `Begin`/`End` pair, and as a cheap way to detect a
missing pop in a debug build.

### `Context::GetID`

```cpp
u32 GetID(const char* str) const;
u32 GetID(const char* str, const char* suffix) const;
u32 GetID(int i) const;
```

| Overload | Parameters | Meaning |
|---|---|---|
| `const char*` | `str` | Stable hash of the label inside the current scope. |
| `const char*`, `const char*` | `str`, `suffix` | Hash of the label with `suffix` hashed on top of it. |
| `int` | `i` | Hash of an integer inside the current scope. |

**Returns:** a `u32` id, never 0.

`GetID` reads the current scope without pushing, so calling it before and after
a `PushID` around the same label gives two different ids. That is the mechanism
behind "derive once, derive many": a widget computes a base id, then XORs or
suffixes it for the sub-parts, so a button's icon, label and hit rect all stay
distinct without any of them needing a scope of their own.

```cpp
const u32 base = ctx.GetID(id);
const u32 icon = ctx.GetID(id, "icon");
const u32 label = ctx.GetID(id, "label");
```

### `Context::HoveredId`

```cpp
u32 HoveredId() const { return hoveredId_; }
```

**Returns:** the id that won this frame's hover arbitration, or 0.

This is the resolved winner, not a geometric test. An item can be under the
pointer and still not be this, because something else claimed the hover first on
the same layer.

### `Context::ActiveId`

```cpp
u32 ActiveId() const { return activeId_; }
```

**Returns:** the id that owns the pointer, or 0.

Set on a left-button press over an interactive item and cleared on release unless
the widget passed `ItemFlags::KeepsActive`. A slider, a scroll area or a
resizable splitter relies on it to keep tracking after the pointer leaves.

### `Context::FocusId`

```cpp
u32 FocusId() const { return focusId_; }
```

**Returns:** the id of the item holding keyboard focus, or 0.

Unlike the other two this survives across frames until something clears it. A
text field sets it on click, clears it on Escape, Tab or a click outside its
bounds, and consults it for caret movement and character input.

### `Context::SetActiveId` and `Context::ClearActiveId`

```cpp
void SetActiveId(u32 id) { activeId_ = id; }
void ClearActiveId() { activeId_ = 0; }
```

**Returns:** nothing, both assign to the same field.

`SetActiveId` is how a widget claims the pointer without a click, for example a
scroll area that should keep flinging after the button came up.
`ClearActiveId` is how a widget gives it up early. Neither one resets the drag
bookkeeping, so a widget that clears the active id mid-drag should not expect
`IsMouseDragging` to agree with it.

### `Context::SetFocusId` and `Context::ClearFocusId`

```cpp
void SetFocusId(u32 id) { focusId_ = id; }
void ClearFocusId() { focusId_ = 0; }
```

**Returns:** nothing, both assign to the same field.

### `Context::SetKeyboardFocusHere`

```cpp
void SetKeyboardFocusHere();
```

**Returns:** nothing, it raises a one-frame flag.

Sets a flag that `NewFrame` clears at the start of the next frame. A widget that
calls it and then calls `ItemBehavior` for its own id on the same frame is
asking to be focused when the pointer or the keyboard is ready to move focus.

### `Context::WantsFocusRequest`

```cpp
bool WantsFocusRequest() const { return wantsFocus_; }
```

**Returns:** true when something has called `SetKeyboardFocusHere` since the
start of this frame.

Read it before deciding whether to clear `FocusId` on a Tab press, or a widget
that asked for focus this frame loses it to the widget that asked last frame.

### `Context::IsItemFocused`

```cpp
bool IsItemFocused() const { return lastItemId_ != 0 && lastItemId_ == focusId_; }
```

**Returns:** true when the most recently registered item is the focused one.

Cheaper than comparing `FocusId()` with an id the caller recomputed, and immune
to the caller getting the hash wrong. Call it right after the item's
`ItemBehavior`.

## Layout

The layout is a stack of regions. Each region holds a content rectangle, a
cursor, and the bookkeeping for the current line: where the line started, how
tall the tallest item on it is, and how far the previous item reached. `NewFrame`
pushes a root region spanning the display, so a frame works even if the
application never calls `BeginLayout`.

### `Context::BeginLayout`

```cpp
void BeginLayout(const Rect& contentRect);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `contentRect` | `const Rect&` | | Absolute region for the children, intersected with the parent's. |

**Returns:** nothing, it pushes a region. Balance it with `EndLayout`.

The rect is clipped to the parent's content region with `IntersectStrict`, so a
child that reaches outside its parent is clipped rather than allowed to paint
over it. The new region inherits the parent's item spacing; it does not read
`ItemSpacingX` and `ItemSpacingY` again.

`BeginLayout` returns nothing, unlike the widget helpers that return an "is it
open" flag. It is a region, not a widget, so there is nothing to skip.

### `Context::BeginLayoutInContent`

```cpp
void BeginLayoutInContent(f32 width, f32 height);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `width` | `f32` | | Width in pixels. Zero or negative means "the rest of the row". |
| `height` | `f32` | | Height in pixels. Zero or negative means "the rest of the column". |

**Returns:** nothing, same pairing rule as `BeginLayout`.

Builds the region from `ContentRegionAvail()` rather than from an absolute rect,
which is what a split layout needs: the left pane takes half the available
width, the right pane takes the other half, and neither has to know where the
container is.

### `Context::EndLayout`

```cpp
void EndLayout();
```

**Returns:** nothing. It refuses to pop the root region, so an unbalanced extra
call cannot leave the layout stack empty.

Advances the parent's cursor to the greater of its current position and the
child region's `max` corner. That uses the region's full extent rather than
where the child's cursor ended, so an empty child still reserves the space it
was given. That is what makes a fixed-height header pane behave predictably.

### `Context::ContentRegion`

```cpp
Rect ContentRegion() const;
```

**Returns:** the innermost region's content rectangle.

This is the full extent of the region, not the unused part of it.

### `Context::ContentRegionAvail`

```cpp
Rect ContentRegionAvail() const;
```

**Returns:** from the cursor to the region's `max` corner, starting at the
current line's top rather than the cursor's y.

The horizontal edge is `cursor.x`, so it shrinks as items are placed. The
vertical edge is `lineY`, so it survives `SameLine` and a multi-item row still
reports its full remaining height.

### Content size helpers

```cpp
f32 ContentWidth() const { return ContentRegionAvail().Width(); }
f32 ContentHeight() const { return ContentRegionAvail().Height(); }
f32 ContentWidthMax() const { return ContentRegion().Width(); }
f32 ContentHeightMax() const { return ContentRegion().Height(); }
Vec2 ContentRegionCenter() const { return ContentRegion().GetCenter(); }
```

**Returns:** `ContentWidth` and `ContentHeight` are the available extents;
`ContentWidthMax` and `ContentHeightMax` are the region's full extents; the
centre is the midpoint of the full region.

The difference between the two widths is the usual source of a control that
overflows its row. `ContentWidth` is what a widget should size itself against;
`ContentWidthMax` is what a centred or right-aligned element should measure from.

### `Context::SetCursorPos`, `SetCursorPosX`, `SetCursorPosY`

```cpp
void SetCursorPos(const Vec2& p);
void SetCursorPosX(f32 x);
void SetCursorPosY(f32 y);
```

| Function | Parameter | Meaning |
|---|---|---|
| `SetCursorPos` | `p` | Absolute screen position. |
| `SetCursorPosX` | `x` | New x, current y. |
| `SetCursorPosY` | `y` | New y, current x. |

**Returns:** nothing, all three move the cursor.

Each one also resets the line bookkeeping: line start, line top, line height,
previous item end and the "there is a previous item" flag. That is deliberate.
A cursor jump is a new line as far as the layout is concerned, so a `SameLine`
after a `SetCursorPos` is a no-op rather than a jump back to a stale position.

`SaveCursor` and `RestoreCursor` are the version that preserves the bookkeeping.

### `Context::CursorPos`

```cpp
Vec2 CursorPos() const;
f32 CursorPosX() const { return CursorPos().x; }
f32 CursorPosY() const { return CursorPos().y; }
```

**Returns:** where the next item will be placed, in screen space.

This is where the sliders and the tabs build their bounds from, which is why a
custom widget usually starts with `const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + size);`.

### `Context::CursorPosLocal`

```cpp
Vec2 CursorPosLocal() const;
```

**Returns:** the cursor relative to the innermost region's `min` corner.

For UI that positions against its own container rather than against the display,
which is what a split layout wants when it pins a control to the right edge of
its pane.

### `Context::SameLine`

```cpp
void SameLine(f32 offsetFromStartX = 0.0f, f32 spacing = -1.0f);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `offsetFromStartX` | `f32` | `0.0f` | Place the item this far from the line's left edge instead of after the previous item. |
| `spacing` | `f32` | `-1.0f` | Gap after the previous item. Negative means the region's `itemSpacingX`. |

**Returns:** nothing. Does nothing when the line has no previous item.

Moves the cursor to the line's y, so the next item sits beside the previous one
rather than below it, and raises the line height to the taller of the two.

### `Context::NewLine`

```cpp
void NewLine();
```

**Returns:** nothing. Advances to the next line at the left edge.

The advance is the current line height plus the vertical item spacing, or the
spacing alone when the line is empty. Resetting the "previous item" flag is what
makes the following `SameLine` a no-op.

### `Context::Spacing`

```cpp
void Spacing();
void Spacing(f32 count);
```

| Overload | Parameter | Meaning |
|---|---|---|
| no arguments | | One vertical gap. |
| `f32` | `count` | Repeat the gap this many times. |

**Returns:** nothing. Each gap is `StyleVarId::GapSmall` tall plus a line break.

`Spacing(count)` loops `count` times with a float counter, so `Spacing(2.5f)`
produces three gaps. Callers pass whole numbers.

### `Context::Dummy`

```cpp
void Dummy(const Vec2& size);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `size` | `const Vec2&` | | Space to reserve. |

**Returns:** nothing. Reserves the space, then breaks the line.

`Dummy` is `ItemSize` followed by `NewLine`, which makes it the tool for leaving
a hole in a layout: draw a control, then a dummy of the width the control should
have had.

### `Context::DummySameLine`

```cpp
void DummySameLine(const Vec2& size);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `size` | `const Vec2&` | | Width to advance past, and height to contribute to the line. |

**Returns:** nothing. Does nothing when the line has no previous item.

Sets the cursor to `prevItemEndX + itemSpacingX + size.x`, so the next `SameLine`
item lines up with where a control of that width would have ended. Use it to
make a label and its control line up without hardcoding the control's width.

### `Context::CursorState`

```cpp
struct CursorState {
    Vec2 cursor;
    f32 lineStartX, lineY, lineHeight, prevItemEndX, prevItemHeight;
    bool hasPrevItem;
};
```

| Field | Type | Meaning |
|---|---|---|
| `cursor` | `Vec2` | Where the next item would go. |
| `lineStartX` | `f32` | Left edge of the current line. |
| `lineY` | `f32` | Top of the current line. |
| `lineHeight` | `f32` | Tallest item on the line so far. |
| `prevItemEndX` | `f32` | X just past the previous item. |
| `prevItemHeight` | `f32` | Height of the previous item. |
| `hasPrevItem` | `bool` | Whether there is a previous item on this line. |

The complete cursor, line bookkeeping included. The plain cursor is not enough
to put a row back exactly where it was: a control drawn at an absolute position
advances the x cursor and the line height, and anything that only saved the
cursor would lose that.

### `Context::SaveCursor` and `Context::RestoreCursor`

```cpp
CursorState SaveCursor() const;
void RestoreCursor(const CursorState& s);
```

| Function | Parameter | Meaning |
|---|---|---|
| `SaveCursor` | | Copies the innermost region's cursor and line state. |
| `RestoreCursor` | `s` | Writes a previously saved state back. |

**Returns:** the state struct, or nothing.

The modal footer uses this to draw its Cancel and Confirm buttons at absolute
positions and then restore the body's cursor, so the body continues from where it
was rather than from the bottom-right corner of the footer.

{% hint style="warning" %}
Save and restore apply to the innermost region. A `RestoreCursor` issued after a
`BeginLayout` writes to the child region, not the one the state came from.
{% endhint %}

### `Context::Indent` and `Context::Unindent`

```cpp
void Indent(f32 indent = 0.0f);
void Unindent(f32 indent = 0.0f);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `indent` | `f32` | `0.0f` | Pixels to shift. Zero or negative means `StyleVarId::GapLarge`. |

**Returns:** nothing.

Both shift three things: the cursor x, the line start x and the region's
`content.min.x`. Because the region's left edge moves, `IndentX` grows with the
number of nested indents and every subsequent row in the region starts indented,
including rows that never call `Indent` themselves.

### `Context::IndentX`

```cpp
f32 IndentX() const;
```

**Returns:** `cursor.x - content.min.x`, so the total indentation in effect.

Because it is measured from the region's own left edge, which `Indent` moves, it
reports the accumulated indent rather than the absolute x.

### `Context::Separator`

```cpp
void Separator();
```

**Returns:** nothing. Draws a hairline and reserves vertical space.

The line spans the region's full width starting at `lineStartX`, is drawn with
`StyleVarId::SeparatorThickness` and `StyleColorId::Separator`, and occupies
`SeparatorThickness + 2 * GapSmall` of layout height.

{% hint style="info" %}
The line is drawn at `lineStartX` but measured against `content.Width()`, so
inside an indented region the right end extends past the visible edge and is
clipped rather than shortened. Draw your own line if the inset matters.
{% endhint %}

### `Context::SeparatorText`

```cpp
void SeparatorText(const char* label);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `label` | `const char*` | | Section title. Null or empty draws a plain separator. |

**Returns:** nothing. Draws a separator, then the label on a pill.

The label is measured in the current font and drawn in `StyleColorId::TextMuted`
on a rounded `StyleColorId::WindowBg` pill that masks the line behind it, with
5 px of horizontal padding and 3 px of vertical offset. A null pointer, an empty
string or a missing font degrades to a plain `Separator`.

## Items and z-order

### `Context::ItemBehavior`

```cpp
ItemResult ItemBehavior(u32 id, const Rect& bounds, ItemFlags flags = ItemFlags::None);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `u32` | | Identity this item is registered under. |
| `bounds` | `const Rect&` | | Hit rectangle, in the same space as the mouse. |
| `flags` | `ItemFlags` | `ItemFlags::None` | Behaviour modifiers. See [`ItemFlags`](#itemflags). |

**Returns:** an `ItemResult` describing hover, press, release and drag, and
records it as the context's last item.

Hover is resolved by draw order and by layer, and both rules exist for a reason.
The first item to claim a layer keeps the hover, which is what stops a container
from stealing it from its own label. A higher layer always takes over, which is
what makes a popup or a modal modal without any special-casing in the widget
code: a modal claims hover at a higher z before its content draws, and
everything beneath it sees `hovered` go false.

Three conditions let an item take the hover from an item that already holds it:
nothing has claimed it yet, it already holds the hover itself, or the item
holding the hover is the same one that held it on the previous frame. That last
condition is what stops the first claimant from locking in. Two widgets
overlapping by a pixel would otherwise freeze on whichever one happened to be
drawn first, forever, because nothing would ever take the hover back.

`AllowOverlap` opts out of exclusivity entirely: that is what lets a slider's
rail and its thumb both report hovered without fighting over the hot id, and the
same for a segmented control's track and its cells.

Presses are gated separately from hover. An input lock, which an open modal or a
drag that owns the pointer raises, suppresses the press but never the hover, so
the widget under the cursor still lights up while the modal is open.

{% hint style="warning" %}
Only `Mouse_Left` produces `pressed`, `held`, `released` and `dragging`.
`ItemResult::clicked` additionally requires the release to land inside `bounds`,
so a press that drags off and comes back does not register as a click. Drag
bookkeeping is only updated while something owns the pointer, which is what keeps
a hover drag from polluting the global drag distance.
{% endhint %}

```cpp
const Rect bounds(ctx.CursorPos(), ctx.CursorPos() + Vec2(120.0f, 28.0f));
const ItemResult r = ctx.ItemBehavior(ctx.GetID("save"), bounds);
if (r.clicked) Save();
ctx.ItemSize(bounds.Size());
ctx.NewLine();
```

### `Context::ItemSize`

```cpp
void ItemSize(const Vec2& size, f32 textBaselineOffsetY = 0.0f);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `size` | `const Vec2&` | | Extent the item occupied. |
| `textBaselineOffsetY` | `f32` | `0.0f` | Accepted for compatibility. The implementation ignores it. |

**Returns:** nothing. It advances the cursor.

Advances the x cursor by `size.x`, raises the line height to at least `size.y`,
and records the item as the previous one on the line.

It also auto-wraps. If there is already an item on this line and adding `size.x`
would overflow the region's right edge, the layout breaks the line first. That is
why a long run of widgets flows onto a second row without any explicit wrapping,
and why a single widget wider than the region never wraps, and overflows.

### `Context::SetItemCursorScreenPos`

```cpp
void SetItemCursorScreenPos(const Vec2& p);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `p` | `const Vec2&` | | Absolute screen position for the next item. |

**Returns:** nothing.

Sets the layout cursor as if a zero-size item were placed there, and resets the
line bookkeeping. Split layouts use it to pin a control to the right edge of a
row: place the control, then move the cursor past it so the next item starts
where it would have.

{% hint style="info" %}
This is a second spelling of `SetCursorPos`. The two have identical
implementations today. Use `SetItemCursorScreenPos` in code that is specifically
about laying out items, and `SetCursorPos` in code that is positioning things.
{% endhint %}

### `Context::IsItemHovered`, `IsItemActive`, `IsItemClicked`

```cpp
bool IsItemHovered() const { return lastItem_.hovered; }
bool IsItemActive() const { return lastItem_.held; }
bool IsItemClicked() const { return lastItem_.clicked; }
```

**Returns:** the hovered, held and clicked flags of the most recent
`ItemBehavior` call.

Convenience accessors for the common case. They read `lastItem_`, which every
`ItemBehavior` call overwrites, including calls made with
`ItemFlags::NoInteraction`, so read them immediately after the call that produced
them and not after drawing the next widget.

### `Context::PushZ`

```cpp
u32 PushZ();
```

**Returns:** the new layer index, one higher than the current one.

Opens a higher draw layer. Popups, context menus and modals use it so their
widgets win the hover and click test over the content beneath, and windows use
it so a window always owns hover over whatever shares its screen space. Pair
every call with `PopZ`.

### `Context::PopZ`

```cpp
void PopZ();
```

**Returns:** nothing. Closes the layer opened by the matching `PushZ`.

Restores the saved layer and then decrements, so a balanced pair returns to the
layer the push was issued on. The z stack is cleared every frame, so an
unbalanced push cannot leak into the next frame.

{% hint style="warning" %}
Nesting deeper than one level does not unwind correctly. Because the pop both
restores and decrements, the innermost pop of a nested sequence lands one layer
lower than it should. The library only ever nests a single layer, so this has
never been hit, but do not build a third level on top of it.
{% endhint %}

### `Context::CurrentZ`

```cpp
u32 CurrentZ() const { return z_; }
```

**Returns:** the layer index currently open, starting at 0 each frame.

## State store

State is a `u32` id to `WidgetState` map that lives as long as the context. A
widget asks for its slot by id and finds everything it left there last frame,
with no bookkeeping in the application.

The `timer` on every entry is advanced by `deltaTime` once per frame in
`NewFrame`, for every live entry whether it is hovered or not. It is an age,
not a hover duration, and it is what `HoverDuration` filters.

### `Context::State`

```cpp
WidgetState& State(u32 id);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `u32` | | Identity of the slot. |

**Returns:** a reference to the slot, creating it with default values on first
use. The reference is stable until the context dies; the map never rehashes an
entry a caller is holding.

### `Context::HasState`

```cpp
bool HasState(u32 id) const;
```

**Returns:** true when the slot already exists, without creating it.

The cheap way to ask whether a widget has ever been drawn, which matters for
one-shot work like a first-time tooltip or a layout that should not animate in.

### Typed state accessors

```cpp
f32 StateF(u32 id, f32 fallback = 0.0f);
void SetStateF(u32 id, f32 v);
bool StateB(u32 id, bool fallback = false);
void SetStateB(u32 id, bool v);
int StateI(u32 id, int fallback = 0);
void SetStateI(u32 id, int v);
Vec2 StateV(u32 id);
void SetStateV(u32 id, const Vec2& v);
```

| Accessor | Field | Default | Meaning |
|---|---|---|---|
| `StateF` / `SetStateF` | `value` | `0.0f` | Primary float: slider position, scroll offset, progress. |
| `StateB` / `SetStateB` | `flag` | `false` | Secondary bool: toggle value, checkbox value. |
| `StateI` / `SetStateI` | `payload` | `0` | Selection index or caret position. |
| `StateV` / `SetStateV` | `vec` | `(0, 0)` | Primary vector: scroll velocity, anchor position. |

**Returns:** the stored value, or the fallback when the slot does not exist
yet. The setters create the slot on demand.

None of the getters create a slot, which is what makes the fallback parameter
meaningful. `SetStateI` stores through a `u32`, so a negative index wraps and
reads back as a large positive one.

```cpp
f32& scroll = ctx.State(ctx.GetID(id)).value;   // direct field access
f32 scroll = ctx.StateF(ctx.GetID(id));           // no creation, 0 when absent
```

### `Context::HoverDuration`

```cpp
f32 HoverDuration(u32 id) const;
```

**Returns:** the slot's `timer` when `hoveredLast` is set, and 0 otherwise.
0 for a slot that does not exist.

The return is gated on the widget setting `hoveredLast` in its own
`ItemBehavior` call. Nothing in the context writes that field, so a widget that
wants a hover delay has to set it, and a widget that does not will read 0
forever.

## Animation hooks

Animations are keyed by id like state, not stored in a timeline the application
manages. A widget asks for its value each frame and gets a continuous number,
whether it is chasing a target or playing a fixed-duration ease.

### `Context::AnimFloat`

```cpp
f32 AnimFloat(u32 id, f32 target, f32 rate);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `u32` | | Animation slot. Distinct from any state slot with the same id. |
| `target` | `f32` | | Value to approach. |
| `rate` | `f32` | | Exponential approach rate per second. Higher is snappier. |

**Returns:** the animated value after advancing it by this frame's delta time.

Damps exponentially rather than easing over a fixed duration, which is the right
model for hover and press feedback because there is no natural end to wait for.
The easing curve is `EaseCubicOut` and is applied only in the fixed-duration
path, so it does not affect this function.

The slot is seeded at `target` on first use, which is why a widget that appears
already hovered fades in from its hovered value rather than from zero.

### `Context::AnimTo`

```cpp
f32 AnimTo(u32 id, f32 target, f32 duration);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `u32` | | Animation slot. |
| `target` | `f32` | | Usually 1 for open, 0 for closed. |
| `duration` | `f32` | | Seconds. Zero or negative falls back to exponential damping. |

**Returns:** the animated value after advancing it.

A 0 to 1 helper for expand, fade and slide transitions. A `duration` above zero
takes a fixed-duration path eased with `EaseCubicOut`; otherwise the rate is
`StyleVarId::ExpandRate`. The two paths are continuous at the boundary, so a
widget can switch between them mid-animation without popping.

### `Context::AnimVec2`

```cpp
Vec2 AnimVec2(u32 id, const Vec2& target, f32 rate);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `id` | `u32` | | Animation slot, in a separate map from the scalars. |
| `target` | `const Vec2&` | | Value to approach, per axis. |
| `rate` | `f32` | | Exponential approach rate per second. |

**Returns:** the animated value after advancing it.

Each axis is damped independently and the result snaps to the target once both
components are within 0.05, so a segmented control's indicator stops moving at
the end of its travel instead of asymptotically creeping. The slot is seeded at
the first target it sees.

### `Context::AnimValue`

```cpp
f32 AnimValue(u32 id) const;
```

**Returns:** the current animated value without advancing it, or 0 when the slot
does not exist.

Use it to read an animation another widget owns, or to sample one without
changing its phase. It reads only the scalar map, so a `Vec2` animation has no
non-advancing accessor.

## Clipping

### `Context::PushClipRect`

```cpp
void PushClipRect(const Rect& rect);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `rect` | `const Rect&` | | Clip region, intersected with the current one. |

**Returns:** nothing. Balance it with `PopClipRect`.

Clipping is intersecting, not replacing, and the previous clip is stored
internally, so nesting works. `PopClipRect` on an empty stack does nothing.

Clipping also affects input: `IsMouseHoveringRect` and `ItemBehavior` test
against the current clip, so a row scrolled out of a viewport is not hoverable
even though its coordinates still sit under the pointer.

### `Context::PushClipRectFullscreen`

```cpp
void PushClipRectFullscreen();   ///< Clip to the current layout region.
```

**Returns:** nothing. The parameterless form of `PushClipRect`.

Clips to the innermost layout region's content rectangle, which is what a
scrolling container wants: the children clip to the pane they are drawn in,
without the container having to remember its own bounds.

### `Context::PopClipRect`

```cpp
void PopClipRect();
```

**Returns:** nothing. Restores the clip that was active before the matching push.

### `Context::ClipRect`

```cpp
const Rect& ClipRect() const { return drawList_.CurrentClip(); }
```

**Returns:** a reference to the draw list's current clip rectangle.

The reference is invalidated by the next `PushClipRect`. It is also the rect to
record alongside a `DrawCheckpoint` when a background box has to be inserted
later.

## Style

The context owns a mutable `Style` seeded from a `Theme`, with a push and pop
stack on top for scoped overrides. `NewFrame` clears the stack every frame, so
a push that is not popped inside the frame leaks nothing: it does not survive to
the next one.

### `Context::GetStyle`

```cpp
Style& GetStyle() { return style_; }
const Style& GetStyle() const { return style_; }
```

**Returns:** the live style, mutable or const.

The mutable overload writes through to the current values without touching the
push stack. That is the right tool for a theme-wide tweak applied once at
startup, and the wrong tool for a per-widget override, which should push and pop
so it cannot leak.

### `Context::GetTheme`

```cpp
const Theme& GetTheme() const { return style_.Theme(); }
```

**Returns:** the theme the current style values were seeded from.

Use it to read something the `Style` does not expose, such as the raw `Palette`
or a button variant's radius.

### `Context::SetTheme`

```cpp
void SetTheme(const Theme& theme);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `theme` | `const Theme&` | | New theme. |

**Returns:** nothing. It calls `Style::Reset`, which re-seeds every value and
discards the style stack.

Calling it mid-frame changes every subsequent widget at once. It does not touch
anything already emitted into the draw list.

### `Context::SetThemePreset`

```cpp
void SetThemePreset(const char* name);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `name` | `const char*` | | `"light"`, `"midnight"`, or anything else for dark. |

**Returns:** nothing. A null pointer is ignored.

Compares with `strcmp`, so the match is exact and case sensitive. Anything
unrecognized, including a typo, silently installs the dark theme.

### `Context::PushStyleColor` and `Context::PopStyleColor`

```cpp
void PushStyleColor(StyleColorId id, const Color& c);
void PopStyleColor(int count = 1);
```

| Function | Parameter | Default | Meaning |
|---|---|---|---|
| `PushStyleColor` | `id`, `c` | | Override one colour entry for the current scope. |
| `PopStyleColor` | `count` | `1` | Restore that many entries. |

**Returns:** nothing, they maintain a stack of previous values.

### `Context::PushStyleVar` and `Context::PopStyleVar`

```cpp
void PushStyleVar(StyleVarId id, f32 v);
void PopStyleVar(int count = 1);
```

| Function | Parameter | Default | Meaning |
|---|---|---|---|
| `PushStyleVar` | `id`, `v` | | Override one numeric entry for the current scope. |
| `PopStyleVar` | `count` | `1` | Restore that many entries. |

**Returns:** nothing.

The two stacks are independent: pushing five colours and popping one var does
not disturb the colours.

```cpp
ctx.PushStyleColor(StyleColorId::Text, ctx.Color(StyleColorId::Danger));
ctx.PushStyleVar(StyleVarId::Alpha, ctx.Alpha() * 0.5f);
DrawSomething(ctx);
ctx.PopStyleVar();
ctx.PopStyleColor();
```

### `Context::Color`, `Var` and `Alpha`

```cpp
Color Color(StyleColorId id) const { return style_.GetColor(id); }
f32 Var(StyleVarId id) const { return style_.GetVar(id); }
f32 Alpha() const { return style_.Alpha(); }
```

**Returns:** the resolved colour, the resolved numeric value, and the style's
global alpha multiplier.

These read through the stack, so a colour pushed by a caller is what a widget
reads. The alpha multiplier is what `AddBox`, `AddLine`, `AddCircle` and
`AddImage` fold into every colour they draw, which is how one `PushStyleVar`
dims an entire subtree without the widgets knowing.

## Text

### `Context::Fonts`

```cpp
FontManager& Fonts() { return fonts_; }
```

**Returns:** the font manager, which owns the family and every baked atlas.

Each distinct pixel size gets its own atlas, created on first request and
re-uploaded only when it actually changed. Going through this reference is for
infrastructure work such as invalidating atlases on a large dpi change;
widgets should use `FontAt`.

### `Context::FontAt`, `SemiboldFontAt`, `MonoFontAt`

```cpp
Font* FontAt(f32 pixelSize);
Font* SemiboldFontAt(f32 pixelSize);
Font* MonoFontAt(f32 pixelSize);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `pixelSize` | `f32` | | Logical size, multiplied by the current dpi scale. |

**Returns:** a `Font*` owned by the manager, or null when the family failed to
load. The pointer is stable for the lifetime of the context.

Atlases are created lazily, so the first call at a new size pays for the bake.
`SemiboldFontAt` and `MonoFontAt` are separate atlases from the regular face at
the same size, not a scaled copy of it.

```cpp
Font* title = ctx.SemiboldFontAt(ctx.Var(StyleVarId::TextSizeTitle));
Font* mono  = ctx.MonoFontAt(ctx.Var(StyleVarId::TextSizeSmall));
```

### `Context::CurrentFont` and `Context::SetCurrentFont`

```cpp
Font* CurrentFont() { return currentFont_; }
void SetCurrentFont(Font* font) { currentFont_ = font; }
```

**Returns:** the font `AddText` and the measurement helpers use when no font is
passed, or nothing for the setter.

Initialized to the body-size regular face by `Initialize`. Set it once around a
block of `Text` calls rather than passing a font to each one, and restore it
afterwards.

### `Context::CalcTextSize`

```cpp
Vec2 CalcTextSize(const char* text, Font* font = nullptr) const;
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | String to measure. |
| `font` | `Font*` | `nullptr` | Font to measure in. Null means `CurrentFont`. |

**Returns:** the measured extent in pixels, or `(0, 0)` when either the font or
the text is null.

`MeasureText` honours embedded newlines, so the height is the number of lines
times the line height.

### `Context::CalcTextSizeX` and `Context::CalcTextHeight`

```cpp
f32 CalcTextSizeX(const char* text, Font* font = nullptr) const;
f32 CalcTextHeight(const char* text, Font* font = nullptr) const;
```

**Returns:** the width, and the font's line height.

`CalcTextHeight` ignores its `text` argument entirely and returns
`Font::LineHeight()`, or 0 when no font is available. It exists so a widget can
size a row without writing the font lookup itself.

{% hint style="info" %}
Widths snap to whole pixels horizontally when glyphs are emitted, because the
atlas is sampled with a linear filter and a fractional origin would smear it.
The returned size is therefore an upper bound on the drawn width by up to half a
pixel per glyph run.
{% endhint %}

### `Context::AddText`

```cpp
f32 AddText(const Vec2& pos, const char* text, const Color& color,
            Font* font = nullptr);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `pos` | `const Vec2&` | | Top-left of the run. `pos.y` is the baseline, not the top. |
| `text` | `const char*` | | UTF-8 string. Newlines are not handled here. |
| `color` | `const Color&` | | Tint. A zero alpha skips the draw entirely. |
| `font` | `Font*` | `nullptr` | Font to draw in. Null means `CurrentFont`. |

**Returns:** the advance width of the run, in pixels. 0 when nothing was drawn.

Decodes UTF-8, applies kerning between adjacent codepoints, snaps each glyph's
x to a whole pixel and emits a quad per non-whitespace glyph into the current
batch under the atlas texture. The batch state is saved and restored around the
run, so a caller's texture and blend are intact afterwards.

Two paths return an advance without drawing: a zero alpha colour, and a font
whose atlas has no texture yet because there is no backend or the first frame
has not uploaded. In the second case measuring is still correct, which is what
keeps headless layout tests producing the right numbers.

### `Context::AddTextCentered`

```cpp
f32 AddTextCentered(const Rect& bounds, const char* text, const Color& color,
                   Font* font = nullptr, bool centerVertically = true);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Box to centre within. |
| `text` | `const char*` | | UTF-8 string. |
| `color` | `const Color&` | | Tint. |
| `font` | `Font*` | `nullptr` | Font. Null means `CurrentFont`. |
| `centerVertically` | `bool` | `true` | Centre on y as well as x. |

**Returns:** the advance width, as `AddText`.

Centres horizontally on both axes when `centerVertically` is set, otherwise it
sits on the box's top edge. Nothing is clipped to `bounds`: the caller pushes a
clip rect first when it needs that.

### `Context::AddTextRightAligned`

```cpp
f32 AddTextRightAligned(const Rect& bounds, const char* text, const Color& color,
                        Font* font = nullptr, bool centerVertically = true);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Box whose right edge the text ends at. |
| `text` | `const char*` | | UTF-8 string. |
| `color` | `const Color&` | | Tint. |
| `font` | `Font*` | `nullptr` | Font. Null means `CurrentFont`. |
| `centerVertically` | `bool` | `true` | Centre on y. |

**Returns:** the advance width, as `AddText`.

### `Context::Text`

```cpp
void Text(const char* fmt, ...);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `fmt` | `const char*` | | `printf`-style format string. |

**Returns:** nothing. It draws one line in `StyleColorId::Text` and advances the
cursor by the measured size.

Formats into a 1024-byte stack buffer and falls back to the heap only when the
result does not fit, so the common case allocates nothing. The text is drawn at
`lineStartX` and `lineY` in the current font and the measured size is passed to
`ItemSize`.

{% hint style="warning" %}
`Text` does not break the line. It calls `ItemSize` and stops, so two consecutive
`Text` calls sit side by side. Call `NewLine` yourself, or use `TextWrapped`,
which ends the line for you.
{% endhint %}

### `Context::TextV`

```cpp
void TextV(const char* fmt, va_list args);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `fmt` | `const char*` | | Format string. |
| `args` | `va_list` | | Already-started argument list. |

**Returns:** nothing. It is the shared body of all four printf-style text calls.

A null format string draws nothing. The argument list is copied with `va_copy`
for the sizing pass so it can be consumed twice without the caller restarting it.

### `Context::TextColored`

```cpp
void TextColored(const Color& c, const char* fmt, ...);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `c` | `const Color&` | | Colour for this run. |
| `fmt` | `const char*` | | Format string. |

**Returns:** nothing.

Pushes `StyleColorId::Text`, calls `TextV`, pops one entry. The push and the pop
are paired around the call, so this is safe to use inside another widget's
pushed scope.

### `Context::TextDisabled`

```cpp
void TextDisabled(const char* fmt, ...);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `fmt` | `const char*` | | Format string. |

**Returns:** nothing.

Pushes `StyleColorId::TextDisabled` onto `StyleColorId::Text` and calls
`TextV`. It replaces the text colour rather than scaling the existing one, so
the disabled colour is exactly `StyleColorId::TextDisabled` whatever the
surrounding theme has pushed.

### `Context::TextWrapped`

```cpp
void TextWrapped(const char* text);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `text` | `const char*` | | UTF-8 string. Not a format string. |

**Returns:** nothing. It wraps, draws every line, then breaks the line.

Wraps to the distance from `lineStartX` to the region's right edge, using the
font's greedy word wrap. A single word longer than the available width is not
broken and will overflow. Unlike `Text`, it ends the line.

## Primitives

These are the `DrawList` primitives with one addition: every colour they take
is multiplied by the style's alpha. That is what lets `PushStyleVar(StyleVarId::Alpha, ...)`
dim an entire subtree.

### `Context::AddBox`

```cpp
void AddBox(const Rect& r, const Color& fill, const CornerRadius& radius = CornerRadius(),
            f32 borderWidth = 0.0f, const Color& borderColor = Color(0, 0, 0, 0),
            f32 glowWidth = 0.0f, const Color& glowColor = Color(0, 0, 0, 0),
            u32 vertexFlags = VF_FLAT);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Screen-space rectangle. |
| `fill` | `const Color&` | | Interior colour. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Per-corner radii, clockwise from top-left. |
| `borderWidth` | `f32` | `0` | Inner border thickness. 0 draws no border. |
| `borderColor` | `const Color&` | `Color(0, 0, 0, 0)` | Border colour. |
| `glowWidth` | `f32` | `0` | Outer glow spread. 0 draws no glow. |
| `glowColor` | `const Color&` | `Color(0, 0, 0, 0)` | Glow colour. |
| `vertexFlags` | `u32` | `VF_FLAT` | Any of the `VF_*` flags. |

**Returns:** nothing, it appends to the draw list.

Identical to `DrawList::AddBox` except that the alpha of the fill, the border
and the glow are each multiplied by the style alpha. See
[Draw list](draw-list.md) for what each flag does.

### `Context::AddLine`

```cpp
void AddLine(const Vec2& a, const Vec2& b, f32 thickness, const Color& c);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `const Vec2&` | | Start point. |
| `b` | `const Vec2&` | | End point. |
| `thickness` | `f32` | | Stroke width in pixels. |
| `c` | `const Color&` | | Stroke colour, alpha-scaled. |

**Returns:** nothing, it appends a capsule with round caps.

### `Context::AddCircle`

```cpp
void AddCircle(const Rect& r, const Color& fill);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Bounding box. The circle is inscribed in it. |
| `fill` | `const Color&` | | Fill colour, alpha-scaled. |

**Returns:** nothing, it appends a filled circle.

The context's version has no border parameters, unlike `DrawList::AddCircle`.
Draw a filled circle over a larger circle's colour to get a ring, or go through
`MutableDrawList` when the border has to be exact.

### `Context::AddImage`

```cpp
void AddImage(const Rect& r, u32 textureId, const Color& tint = Color(1, 1, 1, 1),
              const CornerRadius& radius = CornerRadius());
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `r` | `const Rect&` | | Destination rectangle. |
| `textureId` | `u32` | | Handle from `RegisterTexture`. |
| `tint` | `const Color&` | `Color(1, 1, 1, 1)` | Multiplied into the sampled texel. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Rounded clipping of the image. |

**Returns:** nothing, it appends a textured quad clipped to a rounded box.

Alpha is scaled by the style alpha. Upload straight, non-premultiplied pixel
data: the shader multiplies the texel into the shape colour itself.

## Textures

### `Context::RegisterTexture`

```cpp
u32 RegisterTexture(const TextureDesc& desc, const void* pixels);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `desc` | `const TextureDesc&` | | Size, format, filtering, repeat and sRGB. |
| `pixels` | `const void*` | | Source bytes, or null to allocate uninitialised. |

**Returns:** a handle usable with `AddImage`, or 0 when there is no backend.

The handle is recorded in the context's owned list, so `Shutdown` destroys it.
Uploads pixel data verbatim; the shader expects straight alpha, so do not
premultiply.

### `Context::UpdateTexture`

```cpp
void UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `handle` | `u32` | | Handle from `RegisterTexture`. |
| `desc` | `const TextureDesc&` | | New description. |
| `pixels` | `const void*` | | New source bytes. |

**Returns:** nothing. A no-op without a backend or with a zero handle.

### `Context::UnregisterTexture`

```cpp
void UnregisterTexture(u32 handle);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `handle` | `u32` | | Handle to destroy. |

**Returns:** nothing. Removes the handle from the owned list as well as
destroying it.

A no-op without a backend or with a zero handle. Do not call it twice for the
same handle: the first call removed it from the list, and the second reaches the
backend with a handle it has already destroyed.

### `Context::RegisterTextureFromPixels`

```cpp
u32 RegisterTextureFromPixels(u32 width, u32 height, const u8* rgba,
                              TextureFormat format = TextureFormat::RGBA8);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `width` | `u32` | | Texture width in texels. |
| `height` | `u32` | | Texture height in texels. |
| `rgba` | `const u8*` | | Source bytes, `width * height * bytesPerPixel`. |
| `format` | `TextureFormat` | `TextureFormat::RGBA8` | Channel layout. |

**Returns:** a handle usable with `AddImage`, or 0 without a backend.

Builds a `TextureDesc` with the default sampling state (linear filtering, no
repeat, no sRGB) and forwards to `RegisterTexture`. Nothing is premultiplied.

## Diagnostics

### `Context::DrawStatsSnapshot`

```cpp
const DrawStats& DrawStatsSnapshot() const { return drawList_.Stats(); }
```

**Returns:** the draw list's per-frame counters.

Vertices, indices, quads, draw calls, clip regions and how many quads joined an
existing batch instead of starting one. The last of those is the number that
tells you whether batching is working: a menu frame with a few hundred
primitives should land in single-digit draw calls.

### `Context::CpuFrameMs`

```cpp
f32 CpuFrameMs() const { return cpuFrameMs_; }
```

**Returns:** the last value passed to `SetCpuFrameMs`.

Zero until something sets it. The null backend and the built-in metrics overlay
are the two things in the library that do.

### `Context::SetCpuFrameMs`

```cpp
void SetCpuFrameMs(f32 ms) { cpuFrameMs_ = ms; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `ms` | `f32` | | Milliseconds of CPU time for the frame. |

**Returns:** nothing. It is a plain field write.

The context does not time itself. A host that knows its own frame cost should
push it here so the overlay reports something real.

### `Context::SetMetricsVisible`

```cpp
void SetMetricsVisible(bool visible) { metricsVisible_ = visible; }
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `visible` | `bool` | | Whether to draw the diagnostics overlay. |

**Returns:** nothing. Runtime toggle.

This sets the runtime flag. `ContextConfig::showMetrics` is the start-up value
and is never cleared by this call.

### `Context::MetricsVisible`

```cpp
bool MetricsVisible() const { return metricsVisible_ || config_.showMetrics; }
```

**Returns:** true when either the runtime flag or the config flag is set.

## Input locking

The lock is a counter that suppresses presses everywhere except items flagged
`AllowOverlap`. It is what makes a modal modal: the backdrop still hovers, but
nothing behind it can be clicked.

### `Context::PushInputLock`

```cpp
void PushInputLock() { ++inputLock_; }
```

**Returns:** nothing. It increments a counter, so it nests.

`BeginModal` pushes one and `EndModal` pops one, and a context menu checks
`IsInputLocked` before opening so a right-click inside a modal does not open a
menu on top of it.

{% hint style="warning" %}
The counter is reset to zero in `NewFrame`. A lock is therefore only in force for
the frame it was pushed in, which is what you want for a modal, and what you do
not want for a drag that starts in one frame and continues in the next. Anything
that must survive a frame boundary has to re-push every frame.
{% endhint %}

### `Context::PopInputLock`

```cpp
void PopInputLock() { if (inputLock_ > 0) --inputLock_; }
```

**Returns:** nothing. Guarded, so an unmatched pop cannot underflow.

### `Context::IsInputLocked`

```cpp
bool IsInputLocked() const { return inputLock_ > 0; }
```

**Returns:** true when at least one lock is outstanding.

## Popup dismissal

The contract an open popup has with the frame that follows it. A popup cannot
decide "the user clicked outside me?" in the frame it opens, because by the time
it runs, every widget underneath it has already processed this frame's click.
Deciding then would close the popup and also press the button underneath.

ArgonUI solves this by deferring the decision by exactly one frame. An open
popup publishes its bounds with `SetPopupCapture`. `Render` copies them into the
"was open" slot. At the start of the next frame, `NewFrame` tests this frame's
left click against those bounds, and if the click landed outside them it sets a
suppression flag before a single widget runs. Every `ItemBehavior` then refuses
to claim a press while that flag is set, while hover continues normally.

The visible effect is that clicking off a combo closes it and does not also
press the widget underneath, with no per-widget outside-click handling anywhere
in the library.

### `Context::SetPopupCapture`

```cpp
void SetPopupCapture(const Rect& bounds) {
    nextPopupCapture_ = bounds;
    hasNextPopupCapture_ = true;
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | The popup's rectangle, in the same space as `mousePos`. |

**Returns:** nothing. Call once per frame while a popup is open.

The rect published this frame becomes the dismissal test in the next one. The
bounds are the whole popup rather than its item rows, so clicking the popup's
padding does not dismiss it.

{% hint style="info" %}
The last call in a frame wins, and the flag is cleared at the start of every
frame. That is correct for the single-popup-at-a-time model the library uses,
where two popups cannot be open at once. A design that allows nested popups would
need a stack here, because the inner one would overwrite the outer one's bounds.
{% endhint %}

### `Context::PopupDismissRequested`

```cpp
bool PopupDismissRequested() const { return popupDismiss_; }
```

**Returns:** true when this frame's left click landed outside the popup that was
open at the start of it.

Two different things read this, and both are needed. The widget that owns the
popup uses it to close itself. Every other widget uses it only through
`ItemBehavior`, which suppresses presses while it is set. A widget that
registers itself while the flag is set gets no press, but hover still resolves
normally, so it is still drawn highlighted.

### `Context::PopupWasOpen`

```cpp
bool PopupWasOpen() const { return hasPopupCapture_; }
```

**Returns:** true while a popup was open at the start of this frame.

The companion test for code that needs to know a popup existed without caring
whether the click was outside it, such as an overlay deciding whether to forward
a click to the game underneath.

## Types

### `ItemFlags`

```cpp
enum class ItemFlags : u32 {
    None = 0,
    NoInteraction = 1u << 0,
    NoClip = 1u << 1,
    AllowOverlap = 1u << 2,
    KeepsActive = 1u << 3,
};
```

| Flag | Value | Meaning |
|---|---|---|
| `None` | `0` | Ordinary item: clipped, exclusive, active cleared on release. |
| `NoInteraction` | `1u << 0` | Register nothing. Decorative geometry, or a disabled widget. |
| `NoClip` | `1u << 1` | Test hover against the full bounds, ignoring the clip rect. |
| `AllowOverlap` | `1u << 2` | Opt out of hover exclusivity and of press suppression. |
| `KeepsActive` | `1u << 3` | Do not clear the active id on release. |

OR them together. `NoInteraction` is what a modal backdrop registers as: it
needs to claim hover so nothing beneath it can, but it must never become
pressable itself. `KeepsActive` is what a scroll area or a splitter needs to
keep tracking the pointer after the button comes up.

### `ItemResult`

```cpp
struct ItemResult {
    Rect bounds;
    bool hovered = false;
    bool hoveredExact = false;
    bool held = false;
    bool pressed = false;
    bool released = false;
    bool clicked = false;
    bool doubleClicked = false;
    bool dragging = false;
    Vec2 dragDelta{0.0f, 0.0f};
    Vec2 dragTotal{0.0f, 0.0f};
    Vec2 pressPos{0.0f, 0.0f};
    f32 heldDuration = 0.0f;
};
```

| Field | Type | Meaning |
|---|---|---|
| `bounds` | `Rect` | The rect that was registered, echoed back. |
| `hovered` | `bool` | Pointer is over this item and nothing above it won the arbitration. |
| `hoveredExact` | `bool` | Pointer is over this item, ignoring overlap rules. |
| `held` | `bool` | Mouse is down and this item owns it. |
| `pressed` | `bool` | Went down this frame on this item. |
| `released` | `bool` | Came up this frame on this item. |
| `clicked` | `bool` | `pressed` and `released` within bounds. |
| `doubleClicked` | `bool` | A second click within the double-click window. |
| `dragging` | `bool` | `held`, and the pointer moved past the drag threshold. |
| `dragDelta` | `Vec2` | Movement since the previous frame, while dragging. |
| `dragTotal` | `Vec2` | Total movement since the press. |
| `pressPos` | `Vec2` | Where the press happened, for sliders that jump to the click. |
| `heldDuration` | `f32` | Seconds this item has been held, 0 when it is not. |

`hoveredExact` is the geometric answer and `hovered` is the arbitrated one. A
slider wants `hoveredExact` for its own geometry and `hovered` for deciding
whether to paint the hover state, which is why both are exposed.

`dragTotal` is non-zero for as long as the item is held, which is what a scroll
area integrates. `dragDelta` mirrors `InputState::mouseDelta`, which `NewFrame`
zeroes rather than deriving, so in practice it is always `(0, 0)`; a widget that
needs per-frame motion should difference two positions it stored itself.

### `ContextConfig`

```cpp
struct ContextConfig {
    f32 dpiScale = 1.0f;
    const char* fontFamilyDir = nullptr;
    bool submitAutomatically = true;
    u32 vertexCapacity = 262144;
    u32 indexCapacity = 393216;
    bool showMetrics = false;
    bool verboseLogging = false;
};
```

| Field | Type | Default | Meaning |
|---|---|---|---|
| `dpiScale` | `f32` | `1.0f` | Multiplier applied to every requested font size at `Initialize`. |
| `fontFamilyDir` | `const char*` | `nullptr` | Directory searched for TTFs. Null locates a system font. |
| `submitAutomatically` | `bool` | `true` | Push the frame through the backend inside `Render`. |
| `vertexCapacity` | `u32` | `262144` | Vertices reserved up front. |
| `indexCapacity` | `u32` | `393216` | Indices reserved up front. |
| `showMetrics` | `bool` | `false` | Start with the diagnostics overlay drawn. |
| `verboseLogging` | `bool` | `false` | Stored by the context. Nothing in the library reads it. |

The capacities are soft: the draw list reserves to them at `Initialize` and grows
if a frame needs more, so they are a performance setting rather than a limit. The
defaults are a few thousand primitives' worth.

`submitAutomatically` is the field to change for an overlay that renders from a
hooked present. Set it false and call `backend->BeginFrame`,
`backend->Render(drawList, frame)` and `Present()` from the hook instead; the
context's `Render` then only finalizes and publishes the popup capture.

`dpiScale` here is the start-up scale. The per-frame value comes from
`FrameState::dpiScale`, which `NewFrame` pushes into the font manager on every
frame, so a window dragged between monitors needs no re-initialization.

## See also

- [Input](input.md) - the snapshot this page keeps in `Input()`.
- [Draw list](draw-list.md) - what the primitives append to, and `InsertBackgroundBox`.
- [Style](style.md) - the push and pop stacks this page forwards to.
- [Theme](theme.md) - what `GetTheme` returns and `SetTheme` installs.
- [Fonts](font.md) - the atlases behind `FontAt` and `AddText`.
- [Backend interface](backend.md) - the `IArgonBackend` `Initialize` and `Render` drive.
- [Immediate mode state](../concepts/state.md) - how ids, state and animation fit together.
- [Writing a widget](../guides/writing-a-widget.md) - these APIs in the order a widget uses them.