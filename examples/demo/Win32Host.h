// ArgonUI - examples/demo/Win32Host.h
//
// A deliberately small Win32 host: create a window, pump messages into an
// argon::ui::InputState, and report size/DPI/frame timing. It is here to show
// the smallest thing that works, and to prove that ArgonUI needs nothing else
// from a platform layer.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// GET_X_LPARAM / GET_Y_LPARAM live here, not in windows.h.
#include <windowsx.h>

#include <string>
#include <vector>

#include "argonui/ArgonUI.h"

namespace argonhost {
class Win32Host {
public:
    Win32Host() = default;
    ~Win32Host();

    /// Creates the window. Returns false and fills `error` on failure.
    bool Create(const wchar_t* title, int width, int height, std::string* error);

    HWND Handle() const { return hwnd_; }
    bool IsRunning() const { return running_; }
    void RequestClose() { running_ = false; }

    /// Pumps pending messages. Returns false once the window is closing.
    bool Pump();

    /// Writes this frame's events into `io`.
    void UpdateInput(argon::ui::InputState& io) const;

    int Width() const { return width_; }
    int Height() const { return height_; }
    float ContentScale() const { return contentScale_; }
    const char* Title() const { return "ArgonUI demo"; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool running_ = false;
    float contentScale_ = 1.0f;

    // Last known raw input state.
    mutable int mouseX_ = 0, mouseY_ = 0;
    mutable bool mouseDown_[argon::ui::Mouse_COUNT] = {false, false, false, false, false};
    mutable bool keyDown_[argon::ui::Key_COUNT] = {false};
    mutable bool keyRepeat_[argon::ui::Key_COUNT] = {false};
    mutable float wheel_ = 0.0f;
    mutable unsigned modifiers_ = 0;
    mutable std::vector<argon::ui::Key> pressedThisFrame_;
    mutable std::vector<argon::ui::u32> typedThisFrame_;
};

} // namespace argonhost
