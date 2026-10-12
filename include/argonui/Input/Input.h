// ArgonUI - Input/Input.h
//
// ArgonUI does not talk to Win32 / X11 / Wayland. The host pumps its platform
// events into an InputState (or installs one of the bundled backends) once per
// frame, and the UI consumes that. That is what keeps the core free of platform
// headers and makes the same build work behind D3D11, D3D12, Vulkan or GL.
#pragma once

#include <string>
#include <vector>

#include "../Core/Types.h"

namespace argon::ui {
enum Key : int {
    Key_None = 0,
    Key_Tab, Key_LeftArrow, Key_RightArrow, Key_UpArrow, Key_DownArrow,
    Key_Enter, Key_Escape, Key_Backspace, Key_Delete, Key_Insert,
    Key_LeftShift, Key_LeftControl, Key_LeftAlt, Key_LeftSuper,
    Key_RightShift, Key_RightControl, Key_RightAlt, Key_RightSuper,
    Key_Up, Key_Down, Key_Left, Key_Right, Key_PageUp, Key_PageDown,
    Key_Home, Key_End,
    Key_A, Key_B, Key_C, Key_D, Key_E, Key_F, Key_G, Key_H, Key_I, Key_J,
    Key_K, Key_L, Key_M, Key_N, Key_O, Key_P, Key_Q, Key_R, Key_S, Key_T,
    Key_U, Key_V, Key_W, Key_X, Key_Y, Key_Z,
    Key_0, Key_1, Key_2, Key_3, Key_4, Key_5, Key_6, Key_7, Key_8, Key_9,
    Key_F1, Key_F2, Key_F3, Key_F4, Key_F5, Key_F6,
    Key_F7, Key_F8, Key_F9, Key_F10, Key_F11, Key_F12,
    Key_KeypadDecimal, Key_KeypadDivide, Key_KeypadMultiply, Key_KeypadSubtract,
    Key_KeypadAdd, Key_KeypadEnter, Key_Keypad0, Key_Keypad1, Key_Keypad2,
    Key_Keypad3, Key_Keypad4, Key_Keypad5, Key_Keypad6, Key_Keypad7,
    Key_Keypad8, Key_Keypad9,
    Key_COUNT
};

enum MouseButton : int {
    Mouse_Left = 0,
    Mouse_Right,
    Mouse_Middle,
    Mouse_Button4,
    Mouse_Button5,
    Mouse_COUNT
};

enum KeyMods : u32 {
    Mod_None = 0,
    Mod_Shift = 1u << 0,
    Mod_Ctrl = 1u << 1,
    Mod_Alt = 1u << 2,
    Mod_Super = 1u << 3,
    Mod_CapsLock = 1u << 4,
    Mod_NumLock = 1u << 5,
};

/// Per-frame input snapshot. Filled by the host, read by widgets.
/// Edge state (clicked/released) is computed by Context::NewFrame from the
/// `down` latches, so hosts only ever report "button is/isn't down".
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

    // -- Query helpers -----------------------------------------------------
    bool IsMouseDown(MouseButton b) const { return mouseDown[b]; }
    bool IsMouseClicked(MouseButton b) const { return mouseClicked[b]; }
    bool IsMouseReleased(MouseButton b) const { return mouseReleased[b]; }
    bool IsMouseDoubleClicked(MouseButton b) const { return mouseDoubleClicked[b]; }
    bool IsKeyDown(Key k) const { return keyDown[k]; }
    bool IsKeyPressed(Key k) const { return keyPressed[k]; }
    bool IsKeyReleased(Key k) const { return keyReleased[k]; }

    /// Platform-independent text editing primitives.
    bool KeyChord(Key k, bool ctrl = false, bool shift = false, bool alt = false) const {
        return IsKeyPressed(k) && (this->ctrl() == ctrl) && (this->shifted() == shift) &&
               (this->alt() == alt);
    }

    void ClearFrameEdges();
    void ClearAll();

    static const char* KeyName(Key k);
};

/// Converts a platform key to ArgonUI's. Bundled implementations live in
/// Input/Win32Input.h and Input/GlfwInput.h.
using EventTranslator = bool (*)(u32 platformKey, int platformScancode, Key& outKey);

} // namespace argon::ui
