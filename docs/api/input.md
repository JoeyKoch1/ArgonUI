# Input

ArgonUI reads no platform events. The host pumps its own event queue into an
`InputState` once per frame and the UI consumes that snapshot. That boundary is
what keeps the core free of Win32, X11 and Wayland headers, and what makes the
same build work behind D3D11, D3D12, Vulkan or OpenGL.

This page documents the snapshot, the enums it indexes, and the translator
signature a platform layer implements. Read it when writing the translation
layer, or when a widget does not see a click you know happened.

Source: [`include/argonui/Input/Input.h`](../../include/argonui/Input/Input.h),
implementation in [`src/Input/Input.cpp`](../../src/Input/Input.cpp).

## The host contract

The contract has two halves and only one of them is obvious.

**You fill in the latches.** `mousePos`, `mouseDown[]`, `mouseWheel`,
`keyDown[]`, `modifiers` and `inputCodepoints`. These are level state, not
events: "the button is down", not "the button went down".

**`Context::NewFrame` derives the mouse edges.** It compares `mouseDown[]`
against the previous frame and fills `mouseClicked`, `mouseReleased`,
`mouseDoubleClicked`, `clickPos`, `releasePos` and `mouseDownDuration`. It also
overwrites `mousePrevPos` and zeroes `mouseDelta`, so writing those from the host
either fights the derivation or is discarded.

```cpp
ctx.Input().mousePos = cursor;
ctx.Input().mouseWheel = wheelDelta;
for (int b = 0; b < Mouse_COUNT; ++b) {
    ctx.Input().mouseDown[b] = (buttons & (1u << b)) != 0;
}
ctx.Input().modifiers = mods;
for (u32 cp : typedThisFrame) ctx.Input().inputCodepoints.push_back(cp);

ctx.NewFrame(frameState);   // edges are derived here, before any widget runs
```

{% hint style="warning" %}
The keyboard is the exception. `NewFrame` derives the mouse edges only. The
keyboard edges, `keyPressed`, `keyReleased` and `keyRepeated`, are yours to
fill from your own event queue, and yours to clear each frame. A host that sets
`keyPressed` and never clears it produces a widget that sees the same key press
every frame for the life of the process.
{% endhint %}

`ClearAll` at the top of the frame is the cheap way to be correct. It wipes
both the latches and the edges, so nothing survives from last frame, and then
the host refills the fields it cares about.

Three fields are never written by the library at all: `mouseWheelSign`,
`mouseWheelMoved` and `mouseCursorSetByHost`. They exist so a host can tell the
UI that the wheel moved, in a signed form a list widget can use directly, and
that the pointer is over an ArgonUI surface rather than the game underneath. A
host that never sets them leaves them false and zero, which is the safe default.

## `Key`

```cpp
enum Key : int {
    Key_None = 0,
    // ...
    Key_COUNT
};
```

An unscoped enumeration over `int`, so a `Key` converts to an array index
directly and `Key_COUNT` is the size of every `keyDown`, `keyPressed`,
`keyReleased` and `keyRepeated` array in `InputState`. The values are grouped
below; the groups are for reading, not for arithmetic.

| Group | Members |
|---|---|
| Sentinel | `Key_None` |
| Editing | `Key_Tab`, `Key_Enter`, `Key_Escape`, `Key_Backspace`, `Key_Delete`, `Key_Insert` |
| Arrows | `Key_LeftArrow`, `Key_RightArrow`, `Key_UpArrow`, `Key_DownArrow`, `Key_Up`, `Key_Down`, `Key_Left`, `Key_Right`, `Key_PageUp`, `Key_PageDown`, `Key_Home`, `Key_End` |
| Modifiers | `Key_LeftShift`, `Key_LeftControl`, `Key_LeftAlt`, `Key_LeftSuper`, `Key_RightShift`, `Key_RightControl`, `Key_RightAlt`, `Key_RightSuper` |
| Letters | `Key_A` through `Key_Z` |
| Digits | `Key_0` through `Key_9` |
| Function | `Key_F1` through `Key_F12` |
| Keypad | `Key_KeypadDecimal`, `Key_KeypadDivide`, `Key_KeypadMultiply`, `Key_KeypadSubtract`, `Key_KeypadAdd`, `Key_KeypadEnter`, `Key_Keypad0` through `Key_Keypad9` |
| Sentinel | `Key_COUNT` |

The arrow group has two spellings per direction. `Key_LeftArrow` and friends are
the cursor keys a navigation widget binds to. `Key_Left`, `Key_Right`,
`Key_Up` and `Key_Down` are the logical direction on their own axis, which is
what a text field binds to so that both the arrow keys and the numpad arrows
move the caret. A translation layer maps Win32's `VK_UP` to `Key_UpArrow`; a
widget that wants both binds to `Key_Up` and checks `keyDown` for it.

Modifiers have their own `Key` values as well as the `KeyMods` bits, because
"is shift held" and "did shift go down this frame" are different questions. The
bits answer the first, the `Key` value answers the second.

`Key_COUNT` is a sentinel, not a member. Index with it in a loop bound and with
a range check before a platform key code becomes an index.

## `MouseButton`

```cpp
enum MouseButton : int {
    Mouse_Left = 0,
    Mouse_Right,
    Mouse_Middle,
    Mouse_Button4,
    Mouse_Button5,
    Mouse_COUNT
};
```

| Member | Value | Meaning |
|---|---|---|
| `Mouse_Left` | 0 | Primary button. The only one widgets act on. |
| `Mouse_Right` | 1 | Secondary. Drives `ContextMenu`. |
| `Mouse_Middle` | 2 | Tertiary. |
| `Mouse_Button4` | 3 | First extra button, where the platform reports one. |
| `Mouse_Button5` | 4 | Second extra button. |
| `Mouse_COUNT` | 5 | Array size for every per-button array in `InputState`. |

Three buttons are enough for a menu. `ItemBehavior` only produces
`pressed`, `held`, `released` and `dragging` for `Mouse_Left`, so a widget that
wants right-click or middle-click behaviour reads `Input()` directly rather than
going through the item system. The two extra slots exist because a host with a
five-button mouse should be able to report them without inventing an enum.

## `KeyMods`

```cpp
enum KeyMods : u32 {
    Mod_None = 0,
    Mod_Shift = 1u << 0,
    Mod_Ctrl = 1u << 1,
    Mod_Alt = 1u << 2,
    Mod_Super = 1u << 3,
    Mod_CapsLock = 1u << 4,
    Mod_NumLock = 1u << 5,
};
```

A bitmask in `InputState::modifiers`. OR the bits to build it, test with `&`.

| Bit | Value | Meaning |
|---|---|---|
| `Mod_None` | `0` | Nothing held. The default. |
| `Mod_Shift` | `1u << 0` | Either shift key. |
| `Mod_Ctrl` | `1u << 1` | Either control key. |
| `Mod_Alt` | `1u << 2` | Either alt key. |
| `Mod_Super` | `1u << 3` | The Windows or Command key. |
| `Mod_CapsLock` | `1u << 4` | Caps lock is on. |
| `Mod_NumLock` | `1u << 5` | Num lock is on. |

```cpp
if (in.IsKeyPressed(Key_S) && in.ctrl()) Save();
```

The lock keys are states rather than edges, so `KeyChord` ignores them and only
the four held modifiers take part in a chord test. Sample them live rather than
tracking them per event, or a modifier change that arrives while another window
has focus is lost.

## `InputState`

```cpp
struct InputState {
    // -- Mouse -------------------------------------------------------------
    Vec2 mousePos{0.0f, 0.0f};
    Vec2 mousePrevPos{0.0f, 0.0f};
    Vec2 mouseDelta{0.0f, 0.0f};
    f32 mouseWheel = 0.0f;        ///< Vertical, this frame only.
    f32 mouseWheelH = 0.0f;       ///< Horizontal, this frame only.
    f32 mouseWheelSign = 0.0f;    ///< -1/+1, for list-style scrolling.

    bool mouseDown[Mouse_COUNT] = {false, false, false, false, false};
    bool mouseClicked[Mouse_COUNT] = {false, false, false, false, false};
    bool mouseReleased[Mouse_COUNT] = {false, false, false, false, false};
    bool mouseDoubleClicked[Mouse_COUNT] = {false, false, false, false, false};
    f32 mouseDownDuration[Mouse_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Vec2 clickPos[Mouse_COUNT];   ///< Where the press happened, for drag maths.
    Vec2 releasePos[Mouse_COUNT];
    bool mouseMoved = false;
    bool mouseWheelMoved = false;

    /// Set by the host when the pointer is over an ArgonUI window. Suppresses
    /// hover for widgets covered by an overlay.
    bool mouseCursorSetByHost = false;

    // -- Keyboard ----------------------------------------------------------
    bool keyDown[Key_COUNT] = {false};
    bool keyPressed[Key_COUNT] = {false};
    bool keyReleased[Key_COUNT] = {false};
    bool keyRepeated[Key_COUNT] = {false};
    u32 modifiers = Mod_None;

    bool shifted() const { return (modifiers & Mod_Shift) != 0; }
    bool ctrl() const { return (modifiers & Mod_Ctrl) != 0; }
    bool alt() const { return (modifiers & Mod_Alt) != 0; }
    bool super() const { return (modifiers & Mod_Super) != 0; }

    /// Codepoints typed this frame, in order. Hosts decode UTF-8 and push.
    std::vector<u32> inputCodepoints;
    // ... plus the query helpers and ClearFrameEdges / ClearAll / KeyName below.
};
```

A plain struct of public fields. There is no accessor, no validation and no
allocation beyond the codepoint vector. The host writes it, `NewFrame` derives
into it, and widgets read it.

### Mouse fields

| Field | Type | Default | Written by | Meaning |
|---|---|---|---|---|
| `mousePos` | `Vec2` | `(0, 0)` | Host | Pointer, in the same space the backend renders into. |
| `mousePrevPos` | `Vec2` | `(0, 0)` | `NewFrame` | Pointer last frame. Set from `mousePos` before the frame's widgets run. |
| `mouseDelta` | `Vec2` | `(0, 0)` | `NewFrame` | Zeroed each frame. Currently not derived from the position change. |
| `mouseWheel` | `f32` | `0` | Host | Vertical scroll for this frame, in notches. |
| `mouseWheelH` | `f32` | `0` | Host | Horizontal scroll for this frame. |
| `mouseWheelSign` | `f32` | `0` | Host | `-1` or `+1` for list-style scrolling. Never written by the library, only zeroed by `ClearFrameEdges`. |
| `mouseDown` | `bool[Mouse_COUNT]` | all false | Host | Per-button latch. The input. |
| `mouseClicked` | `bool[Mouse_COUNT]` | all false | `NewFrame` | Went down this frame. Derived. |
| `mouseReleased` | `bool[Mouse_COUNT]` | all false | `NewFrame` | Came up this frame. Derived. |
| `mouseDoubleClicked` | `bool[Mouse_COUNT]` | all false | `NewFrame` | Second click inside the double-click window. Derived. |
| `mouseDownDuration` | `f32[Mouse_COUNT]` | all `0` | `NewFrame` | Seconds held, frozen at the press-to-release interval after release. Derived. |
| `clickPos` | `Vec2[Mouse_COUNT]` | `(0, 0)` | `NewFrame` | Where the press happened. Derived. |
| `releasePos` | `Vec2[Mouse_COUNT]` | `(0, 0)` | `NewFrame` | Where the release happened. Derived. |
| `mouseMoved` | `bool` | `false` | `NewFrame` | The pointer changed position this frame. |
| `mouseWheelMoved` | `bool` | `false` | Host | The wheel moved this frame. Never written by the library at all. |
| `mouseCursorSetByHost` | `bool` | `false` | Host | The pointer is over an ArgonUI window. Suppresses hover for widgets covered by an overlay. Never written by the library at all. |

{% hint style="info" %}
`mouseDelta` is the odd one out. `NewFrame` assigns `mousePrevPos` from
`mousePos` before comparing the two, so `mouseMoved` compares a value against
itself and `mouseDelta` is left at zero. A widget that needs per-frame motion
should store its own previous position rather than reading these.
{% endhint %}

A double click needs two presses within 0.35 s of each other, with the pointer
within 6 px on each axis. After reporting one, the reference time is pushed back
by a full interval so a triple click does not report as a double plus a single.

`clickPos` is what a slider uses to jump to the click: the value jumps to where
the pointer landed, not where it lands on the next frame of movement.

### Keyboard fields

| Field | Type | Default | Written by | Meaning |
|---|---|---|---|---|
| `keyDown` | `bool[Key_COUNT]` | all false | Host | Per-key latch. The input. |
| `keyPressed` | `bool[Key_COUNT]` | all false | Host | Went down this frame. Filled and cleared by the host, not derived. |
| `keyReleased` | `bool[Key_COUNT]` | all false | Host | Came up this frame. Filled and cleared by the host, not derived. |
| `keyRepeated` | `bool[Key_COUNT]` | all false | Host | Auto-repeat fired this frame. Filled and cleared by the host. |
| `modifiers` | `u32` | `Mod_None` | Host | A `KeyMods` bitmask. |
| `inputCodepoints` | `std::vector<u32>` | empty | Host | Text typed this frame, in order. UTF-32. |

Nothing in the library derives a keyboard edge. `NewFrame` touches no key array
at all, so the three edge fields are purely the host's, and the bundled demo
host fills `keyPressed` from its own per-frame event list while leaving
`keyReleased` unset because no widget in the library reads it.

ArgonUI decodes UTF-8 in its own strings, but takes typed input as codepoints so
the host does not have to build a UTF-8 string to hand over a single character.
On Win32 that means pushing `WM_CHAR`'s wide character straight in. On a platform
that delivers UTF-8, decode it once and push the codepoints.

The vector is cleared by `ClearFrameEdges`, so a host that forgets to clear it
replays the same characters every frame.

### Modifier helpers

```cpp
bool shifted() const { return (modifiers & Mod_Shift) != 0; }
bool ctrl() const { return (modifiers & Mod_Ctrl) != 0; }
bool alt() const { return (modifiers & Mod_Alt) != 0; }
bool super() const { return (modifiers & Mod_Super) != 0; }
```

**Returns:** whether that modifier was held when the snapshot was taken.

Convenience wrappers for the common four. They read the mask, they do not
consult the per-key `keyDown` arrays.

## Queries

```cpp
bool IsMouseDown(MouseButton b) const { return mouseDown[b]; }
bool IsMouseClicked(MouseButton b) const { return mouseClicked[b]; }
bool IsMouseReleased(MouseButton b) const { return mouseReleased[b]; }
bool IsMouseDoubleClicked(MouseButton b) const { return mouseDoubleClicked[b]; }
bool IsKeyDown(Key k) const { return keyDown[k]; }
bool IsKeyPressed(Key k) const { return keyPressed[k]; }
bool IsKeyReleased(Key k) const { return keyReleased[k]; }
```

| Function | Returns |
|---|---|
| `IsMouseDown(b)` | The latch: held now. |
| `IsMouseClicked(b)` | The derived edge: went down this frame. |
| `IsMouseReleased(b)` | The derived edge: came up this frame. |
| `IsMouseDoubleClicked(b)` | The derived edge: second click in the window. |
| `IsKeyDown(k)` | The latch: held now. |
| `IsKeyPressed(k)` | The edge: went down this frame. |
| `IsKeyReleased(k)` | The edge: came up this frame. |

None of them range-check. Passing `Key_COUNT` or `Mouse_COUNT` as a key reads one
element past the end of the array, so a platform key code that was not
translated has to be rejected before it becomes an index:

```cpp
const argon::ui::Key k = translate_vk(wParam);
if (k >= 0 && k < argon::ui::Key_COUNT) io.keyDown[k] = down;
```

## `InputState::KeyChord`

```cpp
bool KeyChord(Key k, bool ctrl = false, bool shift = false, bool alt = false) const {
    return IsKeyPressed(k) && (this->ctrl() == ctrl) && (this->shifted() == shift) &&
           (this->alt() == alt);
}
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `k` | `Key` | | Key that must have been pressed this frame. |
| `ctrl` | `bool` | `false` | Required state of `Mod_Ctrl`. |
| `shift` | `bool` | `false` | Required state of `Mod_Shift`. |
| `alt` | `bool` | `false` | Required state of `Mod_Alt`. |

**Returns:** true when the key went down this frame and all three modifiers
match exactly.

The comparison is equality, not "at least". `KeyChord(Key_C, true, false, false)`
is Ctrl+C and deliberately not Ctrl+Shift+C, which a separate call handles. That
makes a set of chords unambiguous where a prefix match would overlap.

`Mod_Super`, `Mod_CapsLock` and `Mod_NumLock` take no part in the test, so a
laptop with Num lock on still types Ctrl+C the same way.

## `InputState::ClearFrameEdges`

```cpp
void ClearFrameEdges();
```

**Returns:** nothing. It clears the per-frame state and leaves the latches
alone.

Zeroes `mouseDelta`, `mouseWheel`, `mouseWheelH` and `mouseWheelSign`. Clears
`mouseClicked`, `mouseReleased` and `mouseDoubleClicked` for every button, and
resets `releasePos` to the origin. Clears `keyPressed`, `keyReleased` and
`keyRepeated` for every key. Empties `inputCodepoints`.

`clickPos`, `mouseDownDuration`, `mousePos`, `mouseDown` and `keyDown` survive.
That is what makes it the right call in the middle of a frame or before a partial
redraw, where the latches are still valid.

```cpp
ctx.Input().ClearFrameEdges();
// ... then fill mouseWheel, keyPressed and inputCodepoints from this frame's events ...
ctx.NewFrame(fs);
```

## `InputState::ClearAll`

```cpp
void ClearAll();
```

**Returns:** nothing. It clears everything and then calls `ClearFrameEdges`.

Zeroes `mousePos`, `mousePrevPos`, every `mouseDown` and every `mouseDownDuration`
and every `keyDown`, and resets `modifiers` to `Mod_None`, before clearing the
frame edges.

This is the call to make at the top of the frame, before refilling from the event
queue. Skipping it is the classic bug: a button the user released while the
window lost focus stays latched down forever, and every widget in the process
looks permanently held.

## `InputState::KeyName`

```cpp
static const char* KeyName(Key k);
```

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `k` | `Key` | | Key to name. |

**Returns:** a static string owned by the library, or `"None"` when `k` is
negative or at least `Key_COUNT`.

Intended for diagnostics and for overlay key hints. The returned pointer is a
string literal in a function-local static table, so it lives forever and must not
be freed or modified.

{% hint style="warning" %}
The name table in `src/Input/Input.cpp` is out of step with the enumeration. It
has no entries for `Key_Up`, `Key_Down`, `Key_Left` or `Key_Right`, so every
name from `Key_Up` onward is shifted by four: `KeyName(Key_Up)` returns
`"PageUp"`, `KeyName(Key_PageUp)` returns `"A"`, and so on through the table.
The array is declared with `Key_COUNT` elements and initialized with 87
initializers, so the last four keypad keys have no entry at all and the function
returns a null pointer for them. Check the result before printing it, and do not
build UI text from it.
{% endhint %}

## `EventTranslator`

```cpp
using EventTranslator = bool (*)(u32 platformKey, int platformScancode, Key& outKey);
```

A plain function pointer, not a class. One translation layer, callable through a
function pointer with no allocation and no virtual dispatch.

| Parameter | Meaning |
|---|---|
| `platformKey` | Native key or scan code, as the platform reports it. |
| `platformScancode` | Native scan code, when the platform has a separate one. |
| `outKey` | Receives the `Key`. Untouched when the call returns false. |

**Returns:** true when the platform key mapped to a `Key` ArgonUI understands,
false when it did not.

```cpp
static bool Win32Translator(u32 vk, int scan, argon::ui::Key& out) {
    switch (vk) {
    case VK_UP:    out = argon::ui::Key_UpArrow; return true;
    case VK_RETURN: out = argon::ui::Key_Enter;  return true;
    default: return false;
    }
}

// Per key-down event:
argon::ui::Key k = argon::ui::Key_None;
if (Win32Translator(wParam, lParam, k)) io.keyDown[k] = true;
```

Returning false is the normal case, not an error: it is how a key the UI does not
care about is skipped, and it is why the caller must range-check the result
before indexing an array. A translator that maps every platform code to
`Key_None` and returns true would make every key press land on index 0.

The header names `Input/Win32Input.h` and `Input/GlfwInput.h` as where the
bundled implementations live. Neither exists in this repository, so write your
own and pass it around as an `EventTranslator`.

## See also

- [Context](context.md) - the object that owns this snapshot and derives the mouse edges.
- [Core types](core-types.md) - `Vec2` and the fixed-width aliases these fields use.
- [Driving the UI from a platform loop](../getting-started/platform-loop.md) - the contract in practice, with worked translations.
- [Writing a widget](../guides/writing-a-widget.md) - reading input from inside an item.