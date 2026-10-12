#include "Win32Host.h"

#include <cstdio>

namespace argonhost {

namespace {

/// Win32 virtual key -> argon::ui::Key.
argon::ui::Key TranslateKey(WPARAM vk) {
    using namespace argon::ui;
    switch (vk) {
    case VK_TAB: return Key_Tab;
    case VK_LEFT: return Key_LeftArrow;
    case VK_RIGHT: return Key_RightArrow;
    case VK_UP: return Key_UpArrow;
    case VK_DOWN: return Key_DownArrow;
    case VK_RETURN: return Key_Enter;
    case VK_ESCAPE: return Key_Escape;
    case VK_BACK: return Key_Backspace;
    case VK_DELETE: return Key_Delete;
    case VK_INSERT: return Key_Insert;
    case VK_SHIFT: return Key_LeftShift;
    case VK_CONTROL: return Key_LeftControl;
    case VK_MENU: return Key_LeftAlt;
    case VK_LWIN: case VK_RWIN: return Key_LeftSuper;
    case VK_PRIOR: return Key_PageUp;
    case VK_NEXT: return Key_PageDown;
    case VK_HOME: return Key_Home;
    case VK_END: return Key_End;
    default: break;
    }
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(Key_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<Key>(Key_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return static_cast<Key>(Key_F1 + (vk - VK_F1));
    return Key_None;
}

} // namespace

Win32Host::~Win32Host() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Win32Host::Create(const wchar_t* title, int width, int height, std::string* error) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Win32Host::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ArgonUIHost";
    if (RegisterClassExW(&wc) == 0) {
        if (error != nullptr) *error = "RegisterClassEx failed";
        return false;
    }

    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, L"ArgonUIHost", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                            wc.hInstance, this);
    if (hwnd_ == nullptr) {
        if (error != nullptr) *error = "CreateWindowEx failed";
        return false;
    }

    width_ = width;
    height_ = height;
    running_ = true;
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
    return true;
}

bool Win32Host::Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return running_;
}

LRESULT CALLBACK Win32Host::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Win32Host* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Host*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Win32Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    using namespace argon::ui;

    switch (msg) {
    case WM_DESTROY:
        if (self != nullptr) self->running_ = false;
        PostQuitMessage(0);
        return 0;

    case WM_SIZE: {
        if (self != nullptr) {
            self->width_ = LOWORD(lParam);
            self->height_ = HIWORD(lParam);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        if (self != nullptr) {
            self->contentScale_ = HIWORD(wParam) / 96.0f;
            // Let Windows resize the window for us.
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (self != nullptr) {
            self->mouseX_ = GET_X_LPARAM(lParam);
            self->mouseY_ = GET_Y_LPARAM(lParam);
        }
        return 0;

    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        if (self != nullptr) {
            const int b = (msg == WM_LBUTTONDOWN) ? Mouse_Left
                        : (msg == WM_RBUTTONDOWN) ? Mouse_Right : Mouse_Middle;
            self->mouseDown_[b] = true;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        if (self != nullptr) {
            const int b = (msg == WM_LBUTTONUP) ? Mouse_Left
                        : (msg == WM_RBUTTONUP) ? Mouse_Right : Mouse_Middle;
            self->mouseDown_[b] = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (self != nullptr) {
            self->wheel_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        }
        return 0;

    case WM_MOUSEHWHEEL:
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (self != nullptr) {
            const argon::ui::Key k = TranslateKey(wParam);
            if (k != argon::ui::Key_None) {
                const bool wasDown = self->keyDown_[k];
                self->keyDown_[k] = true;
                // Bit 30 of lParam marks auto-repeat; report repeats separately so
                // text fields can behave like a real editor.
                const bool repeat = (lParam & (1L << 30)) != 0;
                if (!wasDown || repeat) self->pressedThisFrame_.push_back(k);
                self->keyRepeat_[k] = repeat;
            }
        }
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (self != nullptr) {
            const argon::ui::Key k = TranslateKey(wParam);
            if (k != argon::ui::Key_None) {
                self->keyDown_[k] = false;
                self->keyRepeat_[k] = false;
            }
        }
        return 0;
    }

    case WM_CHAR: {
        if (self != nullptr) {
            const wchar_t c = static_cast<wchar_t>(wParam);
            if (c == L'\r' || c == L'\n') {
                self->typedThisFrame_.push_back(0x0Du);
            } else if (c >= 0x20 && c != 0x7F) {
                // Surrogate pair: queue the high half now, the low half arrives in
                // the next WM_CHAR.
                self->typedThisFrame_.push_back(static_cast<argon::ui::u32>(c));
            }
        }
        return 0;
    }

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32Host::UpdateInput(argon::ui::InputState& io) const {
    // Position is reported in the same space the backend renders into, so a
    // scaled/high-DPI display only needs the scale applied here.
    io.mousePos = Vec2(static_cast<f32>(mouseX_), static_cast<f32>(mouseY_));
    io.mouseWheel = wheel_;
    io.mouseWheelH = 0.0f;
    wheel_ = 0.0f;

    for (int b = 0; b < argon::ui::Mouse_COUNT; ++b) {
        io.mouseDown[b] = mouseDown_[b];
    }

    // Modifier state is sampled live rather than tracked event-by-event.
    io.modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) io.modifiers |= argon::ui::Mod_Shift;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) io.modifiers |= argon::ui::Mod_Ctrl;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) io.modifiers |= argon::ui::Mod_Alt;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0) io.modifiers |= argon::ui::Mod_Super;
    if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) io.modifiers |= argon::ui::Mod_CapsLock;
    if ((GetKeyState(VK_NUMLOCK) & 0x0001) != 0) io.modifiers |= argon::ui::Mod_NumLock;

    for (const argon::ui::Key k : pressedThisFrame_) {
        if (k >= 0 && k < argon::ui::Key_COUNT) io.keyPressed[k] = true;
    }
    for (int i = 0; i < argon::ui::Key_COUNT; ++i) {
        io.keyDown[i] = keyDown_[i];
        io.keyRepeated[i] = keyRepeat_[i];
    }
    io.inputCodepoints = typedThisFrame_;

    pressedThisFrame_.clear();
    typedThisFrame_.clear();
}

} // namespace argonhost