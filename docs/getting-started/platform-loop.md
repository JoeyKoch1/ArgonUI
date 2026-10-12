# Driving the UI from a platform loop

ArgonUI never reads an event. You fill an `InputState` once per frame and the
context derives everything it needs from it. This page is the contract between
those two halves.

## The rule

You fill in:

- `mousePos`
- `mouseDown[]`
- `mouseWheel`
- `keyDown[]`
- `inputCodepoints`
- `modifiers`

The context derives everything else. `mouseClicked`, `mouseReleased`,
`mouseDoubleClicked`, `keyPressed`, `keyReleased`, `mouseDelta`, held durations
and click positions are all computed in `NewFrame`. Writing them yourself will
fight the derivation.

## Order of operations

```cpp
io.ClearAll();                    // wipe last frame's edges
// ... fill io from your events ...
ctx.NewFrame(fs);                 // derives edges, advances animations
// ... widgets ...
```

`ClearAll` resets the `mouseDown` and `keyDown` latches. If you skip it, a
button you stopped pressing stays held forever.

## Translating a platform

### Win32

```cpp
// WM_MOUSEMOVE
io.mousePos = { (float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam) };

// WM_LBUTTONDOWN / WM_LBUTTONUP
io.mouseDown[argon::ui::Mouse_Left] = down;

// WM_MOUSEWHEEL
io.mouseWheel += (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

// WM_KEYDOWN / WM_KEYUP
const argon::ui::Key k = translate_vk(wParam);
io.keyDown[k] = down;

// WM_CHAR
// ArgonUI takes UTF-32 codepoints, so push the wide char straight in. It
// decodes UTF-8 itself when you hand it strings.
io.inputCodepoints.push_back((argon::ui::u32)c);
```

Sample the modifier keys live rather than tracking them per event. That avoids
losing a state change when a key event arrives while another window has focus:

```cpp
io.modifiers = 0;
if (GetKeyState(VK_SHIFT)   & 0x8000) io.modifiers |= argon::ui::Mod_Shift;
if (GetKeyState(VK_CONTROL) & 0x8000) io.modifiers |= argon::ui::Mod_Ctrl;
if (GetKeyState(VK_MENU)    & 0x8000) io.modifiers |= argon::ui::Mod_Alt;
```

The demo's `examples/demo/Win32Host.cpp` is a working translation layer you can
lift whole.

### GLFW

```cpp
io.mousePos = { cursorX, cursorY };
for (int b = 0; b < argon::ui::Mouse_COUNT; ++b) {
    io.mouseDown[b] = glfwGetMouseButton(window, b) == GLFW_PRESS;
}
io.mouseWheel = -glfwGetScrollOffset(window, GLFW_MOUSE_WHEEL_Y) / 120.0f;
io.modifiers  = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) ? argon::ui::Mod_Shift : 0;
```

GLFW reports scroll in the opposite direction to what widgets expect, hence the
negation.

### SDL

```cpp
io.mousePos = { (float)e->motion.x, (float)e->motion.y };
io.mouseWheel = e->wheel.y;   // SDL3
io.modifiers = keymod_to_argon(SDL_GetModState());
```

## Focus and occlusion

Two `FrameState` fields matter more than they look:

```cpp
fs.focused  = GetForegroundWindow() == hwnd;
fs.occluded = IsIconic(hwnd);   // minimised
```

`occluded` makes `BeginFrame` return false on the backend, which skips the whole
frame. Set it when the window is minimised or on another virtual desktop,
otherwise ArgonUI keeps submitting work nobody sees. Note that
`DisplaySize` of zero already implies occlusion, so the common
`fs.occluded = w <= 0 || h <= 0` covers the worst case.

`focused` is used to suppress hover. A menu that stays highlighted while you
click another window looks broken.

## Frame timing

```cpp
static LARGE_INTEGER freq, last;
QueryPerformanceFrequency(&freq);
QueryPerformanceCounter(&last);

LARGE_INTEGER now;
QueryPerformanceCounter(&now);
const float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
last = now;
```

Pass the real elapsed time. `NewFrame` clamps it to 250 ms so a debugger pause
cannot teleport animations, but feeding a constant 1/60 while the real frame took
200 ms makes every transition run five times too fast.

## Multiple contexts

ArgonUI keeps the current context in a thread-local, so a second context on
another thread is independent. Free functions like `Button` read the current
one, which is convenient for single-context code and a trap for multi-context
code. If you render a menu and a HUD from two contexts on one thread, switch
with `Context::SetCurrent` before each and call the widgets through an explicit
reference where the API offers one.

## Next

- [Input reference](../api/input.md)
- [Rendering into someone else's device](../guides/overlay.md)