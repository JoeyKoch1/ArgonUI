#include "argonui/Input/Input.h"

namespace argon::ui {
void InputState::ClearFrameEdges() {
    mouseDelta = Vec2(0.0f, 0.0f);
    mouseWheel = 0.0f;
    mouseWheelH = 0.0f;
    mouseWheelSign = 0.0f;

    for (int i = 0; i < Mouse_COUNT; ++i) {
        mouseClicked[i] = false;
        mouseReleased[i] = false;
        mouseDoubleClicked[i] = false;
        releasePos[i] = Vec2(0.0f, 0.0f);
    }
    for (int i = 0; i < Key_COUNT; ++i) {
        keyPressed[i] = false;
        keyReleased[i] = false;
        keyRepeated[i] = false;
    }
    inputCodepoints.clear();
}

void InputState::ClearAll() {
    mousePos = Vec2(0.0f, 0.0f);
    mousePrevPos = Vec2(0.0f, 0.0f);
    for (int i = 0; i < Mouse_COUNT; ++i) {
        mouseDown[i] = false;
        mouseDownDuration[i] = 0.0f;
    }
    for (int i = 0; i < Key_COUNT; ++i) {
        keyDown[i] = false;
    }
    modifiers = Mod_None;
    ClearFrameEdges();
}

const char* InputState::KeyName(Key k) {
    static const char* names[Key_COUNT] = {
        "None", "Tab", "Left", "Right", "Up", "Down",
        "Enter", "Escape", "Backspace", "Delete", "Insert",
        "LShift", "LCtrl", "LAlt", "LSuper",
        "RShift", "RCtrl", "RAlt", "RSuper",
        "PageUp", "PageDown", "Home", "End",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "F1", "F2", "F3", "F4", "F5", "F6",
        "F7", "F8", "F9", "F10", "F11", "F12",
        "KP.Decimal", "KP.Divide", "KP.Multiply", "KP.Subtract",
        "KP.Add", "KP.Enter", "KP.0", "KP.1", "KP.2",
        "KP.3", "KP.4", "KP.5", "KP.6", "KP.7",
        "KP.8", "KP.9"
    };
    if (k < 0 || k >= Key_COUNT) return "None";
    return names[k];
}

} // namespace argon::ui
