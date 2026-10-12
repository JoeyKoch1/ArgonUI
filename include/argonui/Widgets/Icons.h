// ArgonUI - Widgets/Icons.h
//
// A stroke icon set drawn from the same primitives as everything else: no icon
// font, no SVG rasteriser, no texture atlas. Every glyph is a few lines, arcs
// and rings on a 24x24 design grid, stroked with the caller's thickness and
// tinted with the caller's colour, so an icon inherits hover/active states for
// free and stays crisp at any DPI.
//
// Add a new icon by appending to the enum and adding one `case` to Icons.cpp.
// The grid is 0..1 with the origin at the top-left.
#pragma once

#include "../Core/Color.h"
#include "../Core/Rect.h"
#include "../Core/Types.h"
#include "../Draw/DrawList.h"

namespace argon::ui {
enum class Icon : u16 {
    None = 0,

    // Navigation / chrome
    Search, Close, Check, Minus, Plus,
    ChevronUp, ChevronDown, ChevronLeft, ChevronRight,
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
    Menu, More, DotsHorizontal, DotsVertical,
    ExternalLink,

    // Files
    Folder, FolderOpen, File, Save, Download, Upload, Image, Trash, Copy, Clipboard,

    // Toggles & settings
    Settings, Sliders, Eye, EyeOff, Lock, Unlock, Bell, BellOff,
    Power, Refresh, Rotate, Link, Unlink,

    // Domain
    User, Users, Tag, Bolt, Cube, Layers, Target, Crosshair, Shield, Star, Heart,
    Home, Grid, List, Palette, Sparkle, Info, Warning, Error, CheckCircle,
    Play, Pause, Stop, Expand, Collapse, Filter, SortAscending, SortDescending,

    COUNT
};

/// How much of the 24x24 grid the artwork actually occupies. Keeping icons on a
/// shared optical grid is the single biggest thing that makes a hand-rolled set
/// look professional rather than hand-made.
struct IconMetrics {
    f32 inset = 0.14f;   ///< Fraction of the box kept as padding on each side.
};

/// Draws `icon` inside `box`, scaled to fit, stroked at `thickness` pixels.
/// The drawing is centred in the box and never exceeds it.
void DrawIcon(DrawList& list, Icon icon, const Rect& box, f32 thickness, const Color& color,
              f32 alpha = 1.0f);

/// Convenience wrapper for the current context (assumes box is already sized).
void DrawIcon(Icon icon, const Rect& box, f32 thickness, const Color& color, f32 alpha = 1.0f);

/// Square box of side `size` centred on `center`. Helper for list rows.
inline Rect IconBox(const Vec2& center, f32 size) {
    const f32 h = size * 0.5f;
    return Rect(center.x - h, center.y - h, center.x + h, center.y + h);
}

} // namespace argon::ui
