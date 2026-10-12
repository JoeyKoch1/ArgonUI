// ArgonUI - examples/demo/DemoMenu.h
#pragma once

#include "argonui/ArgonUI.h"

namespace argondemo {

/// All demo state. Kept in one place so the whole menu is data-driven and the
/// widget code stays readable.
struct MenuState {
    // Tabs.
    int tab = 0;

    // Sidebar.
    int page = 0;
    int sidebarBadges[6] = {0, 2, 0, 1, 0, 0};

    // Visuals tab.
    bool espEnabled = true;
    bool outOfScreenArrows = true;
    bool soundEsp = false;
    bool boxEnabled = true;
    bool healthBar = false;
    bool nameTags = true;
    bool tracers = false;
    f32 fov = 90.0f;
    f32 boxThickness = 1.5f;
    f32 smoothness = 3.0f;
    int entityMode = 1;   // 0 = Visible, 1 = Behind wall
    bool wallChams = false;
    bool glow = false;
    f32 glowStrength = 0.5f;

    // Ammo tab.
    int ammoPreset = 0;
    float ammoValues[4] = {30.0f, 90.0f, 180.0f, 4476.0f};

    // Modals / popups.
    bool modalOpen = false;
    bool contextOpen = false;

    // Search.
    std::string query;

    // Combos.
    const char* skins[4] = {"Desert Eagle", "AK-47", "M4A1-S", "AWP"};
    int skinIndex = 0;

    int teamMode = 0;
    const char* teams[3] = {"Counter-Terrorists", "Both teams", "Terrorists"};

    bool themeLight = false;
    bool showMetrics = false;
};

/// Draws one frame of the demo menu into `ctx`.
void DrawMenu(argon::ui::Context& ctx, MenuState& state);

} // namespace argondemo