#include "argonui/Widgets/Icons.h"

#include <cmath>

#include "argonui/Context.h"

namespace argon::ui {

namespace {

/// Small helper so each icon case reads like a sketch rather than arithmetic.
struct Pen {
    DrawList& list;
    Vec2 origin;
    f32 scale;
    f32 thickness;
    Color color;

    Vec2 P(f32 x, f32 y) const { return Vec2(origin.x + x * scale, origin.y + y * scale); }

    void Line(f32 x1, f32 y1, f32 x2, f32 y2) const {
        list.AddLine(P(x1, y1), P(x2, y2), thickness, color);
    }
    void Box(f32 x, f32 y, f32 w, f32 h, f32 radius = 0.0f) const {
        const Vec2 a = P(x, y);
        const Vec2 b = P(x + w, y + h);
        list.AddBox(Rect(a, b), color, CornerRadius(radius * scale));
    }
    void Circle(f32 cx, f32 cy, f32 r) const {
        const Vec2 c = P(cx, cy);
        list.AddCircle(Rect(c.x - r * scale, c.y - r * scale, c.x + r * scale, c.y + r * scale), color);
    }
    /// Degrees clockwise from 3 o'clock (screen Y grows down).
    void Arc(f32 cx, f32 cy, f32 r, f32 startDeg, f32 sweepDeg) const {
        const Vec2 c = P(cx, cy);
        list.AddArc(Rect(c.x - r * scale, c.y - r * scale, c.x + r * scale, c.y + r * scale),
                    startDeg, sweepDeg, thickness, color);
    }
    void Tri(f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3) const {
        list.AddTriangle(P(x1, y1), P(x2, y2), P(x3, y3), color);
    }
    /// Closed polyline from a flat [x0,y0,x1,y1,...] array of 2*count values.
    void Poly(const f32* pts, int count) const {
        for (int i = 0; i < count; ++i) {
            const int j = (i + 1) % count;
            Line(pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1]);
        }
    }
    /// N-pointed star.
    void Star(f32 cx, f32 cy, f32 outer, f32 inner, f32 startDeg) const {
        for (int i = 0; i < 5; ++i) {
            const f32 ao = (startDeg + i * 72.0f) * kPi / 180.0f;
            const f32 ai = (startDeg + i * 72.0f + 36.0f) * kPi / 180.0f;
            Line(cx + std::cos(ao) * outer, cy + std::sin(ao) * outer,
                 cx + std::cos(ai) * inner, cy + std::sin(ai) * inner);
        }
    }
};

constexpr f32 kDeg = kPi / 180.0f;

} // namespace

void DrawIcon(DrawList& list, Icon icon, const Rect& box, f32 thickness, const Color& color,
              f32 alpha) {
    if (icon == Icon::None || box.IsEmpty()) return;

    // Fit the 24-unit grid into the box, preserving aspect ratio.
    const f32 side = Min(box.Width(), box.Height());
    const f32 inset = side * 0.16f;
    const f32 scale = (side - inset * 2.0f) / 24.0f;
    const Vec2 origin(box.GetCenter().x - 12.0f * scale, box.GetCenter().y - 12.0f * scale);

    const Pen pen{list, origin, scale, Max(thickness, 0.75f), color.WithAlpha(color.a * alpha)};

    switch (icon) {
    // ---- navigation ---------------------------------------------------
    case Icon::Search:
        pen.Circle(10.5f, 10.5f, 6.0f);
        pen.Line(15.0f, 15.0f, 20.5f, 20.5f);
        break;
    case Icon::Close:
        pen.Line(6.0f, 6.0f, 18.0f, 18.0f);
        pen.Line(18.0f, 6.0f, 6.0f, 18.0f);
        break;
    case Icon::Check:
        pen.Line(4.5f, 12.5f, 9.5f, 17.5f);
        pen.Line(9.5f, 17.5f, 19.5f, 6.5f);
        break;
    case Icon::Minus:
        pen.Line(5.0f, 12.0f, 19.0f, 12.0f);
        break;
    case Icon::Plus:
        pen.Line(12.0f, 5.0f, 12.0f, 19.0f);
        pen.Line(5.0f, 12.0f, 19.0f, 12.0f);
        break;
    case Icon::ChevronUp:
        pen.Line(5.5f, 15.0f, 12.0f, 8.5f);
        pen.Line(12.0f, 8.5f, 18.5f, 15.0f);
        break;
    case Icon::ChevronDown:
        pen.Line(5.5f, 9.0f, 12.0f, 15.5f);
        pen.Line(12.0f, 15.5f, 18.5f, 9.0f);
        break;
    case Icon::ChevronLeft:
        pen.Line(15.0f, 5.5f, 8.5f, 12.0f);
        pen.Line(8.5f, 12.0f, 15.0f, 18.5f);
        break;
    case Icon::ChevronRight:
        pen.Line(9.0f, 5.5f, 15.5f, 12.0f);
        pen.Line(15.5f, 12.0f, 9.0f, 18.5f);
        break;
    case Icon::ArrowLeft:
        pen.Line(19.0f, 12.0f, 5.0f, 12.0f);
        pen.Line(10.5f, 6.5f, 5.0f, 12.0f);
        pen.Line(5.0f, 12.0f, 10.5f, 17.5f);
        break;
    case Icon::ArrowRight:
        pen.Line(5.0f, 12.0f, 19.0f, 12.0f);
        pen.Line(13.5f, 6.5f, 19.0f, 12.0f);
        pen.Line(19.0f, 12.0f, 13.5f, 17.5f);
        break;
    case Icon::ArrowUp:
        pen.Line(12.0f, 19.0f, 12.0f, 5.0f);
        pen.Line(6.5f, 10.5f, 12.0f, 5.0f);
        pen.Line(12.0f, 5.0f, 17.5f, 10.5f);
        break;
    case Icon::ArrowDown:
        pen.Line(12.0f, 5.0f, 12.0f, 19.0f);
        pen.Line(6.5f, 13.5f, 12.0f, 19.0f);
        pen.Line(12.0f, 19.0f, 17.5f, 13.5f);
        break;
    case Icon::Menu:
        pen.Line(4.0f, 7.0f, 20.0f, 7.0f);
        pen.Line(4.0f, 12.0f, 20.0f, 12.0f);
        pen.Line(4.0f, 17.0f, 20.0f, 17.0f);
        break;
    case Icon::More:
    case Icon::DotsVertical:
        pen.Circle(12.0f, 5.5f, 1.4f);
        pen.Circle(12.0f, 12.0f, 1.4f);
        pen.Circle(12.0f, 18.5f, 1.4f);
        break;
    case Icon::DotsHorizontal:
        pen.Circle(5.5f, 12.0f, 1.4f);
        pen.Circle(12.0f, 12.0f, 1.4f);
        pen.Circle(18.5f, 12.0f, 1.4f);
        break;
    case Icon::ExternalLink:
        pen.Line(14.0f, 5.0f, 19.0f, 5.0f);
        pen.Line(19.0f, 5.0f, 19.0f, 10.0f);
        pen.Line(19.0f, 10.0f, 11.0f, 18.0f);
        pen.Line(5.0f, 19.0f, 5.0f, 13.0f);
        pen.Line(5.0f, 13.0f, 11.0f, 13.0f);
        break;

    // ---- files ---------------------------------------------------------
    case Icon::Folder:
        pen.Line(3.5f, 19.5f, 3.5f, 6.0f);
        pen.Line(3.5f, 6.0f, 9.5f, 6.0f);
        pen.Line(9.5f, 6.0f, 11.5f, 8.5f);
        pen.Line(11.5f, 8.5f, 20.5f, 8.5f);
        pen.Line(20.5f, 8.5f, 20.5f, 19.5f);
        pen.Line(20.5f, 19.5f, 3.5f, 19.5f);
        break;
    case Icon::FolderOpen:
        pen.Line(3.5f, 18.5f, 3.5f, 6.0f);
        pen.Line(3.5f, 6.0f, 9.5f, 6.0f);
        pen.Line(9.5f, 6.0f, 11.5f, 8.5f);
        pen.Line(11.5f, 8.5f, 19.0f, 8.5f);
        pen.Line(5.5f, 18.5f, 7.5f, 11.0f);
        pen.Line(7.5f, 11.0f, 21.0f, 11.0f);
        pen.Line(21.0f, 11.0f, 19.0f, 18.5f);
        pen.Line(19.0f, 18.5f, 5.5f, 18.5f);
        break;
    case Icon::File:
        pen.Line(5.5f, 3.5f, 14.5f, 3.5f);
        pen.Line(14.5f, 3.5f, 18.5f, 7.5f);
        pen.Line(18.5f, 7.5f, 18.5f, 20.5f);
        pen.Line(18.5f, 20.5f, 5.5f, 20.5f);
        pen.Line(5.5f, 20.5f, 5.5f, 3.5f);
        pen.Line(14.5f, 3.5f, 14.5f, 7.5f);
        pen.Line(14.5f, 7.5f, 18.5f, 7.5f);
        break;
    case Icon::Save:
        pen.Line(4.0f, 4.0f, 16.5f, 4.0f);
        pen.Line(16.5f, 4.0f, 20.0f, 7.5f);
        pen.Line(20.0f, 7.5f, 20.0f, 20.0f);
        pen.Line(20.0f, 20.0f, 4.0f, 20.0f);
        pen.Line(4.0f, 20.0f, 4.0f, 4.0f);
        pen.Box(8.0f, 4.0f, 8.0f, 5.0f);
        pen.Box(7.5f, 13.5f, 9.0f, 6.5f, 1.0f);
        break;
    case Icon::Download:
        pen.Line(12.0f, 3.5f, 12.0f, 14.5f);
        pen.Line(7.5f, 10.0f, 12.0f, 14.5f);
        pen.Line(12.0f, 14.5f, 16.5f, 10.0f);
        pen.Line(4.0f, 19.0f, 20.0f, 19.0f);
        break;
    case Icon::Upload:
        pen.Line(12.0f, 15.0f, 12.0f, 4.0f);
        pen.Line(7.5f, 8.5f, 12.0f, 4.0f);
        pen.Line(12.0f, 4.0f, 16.5f, 8.5f);
        pen.Line(4.0f, 19.5f, 20.0f, 19.5f);
        break;
    case Icon::Image:
        pen.Box(3.5f, 5.0f, 17.0f, 14.0f, 2.0f);
        pen.Circle(9.0f, 10.0f, 1.8f);
        pen.Line(4.5f, 17.5f, 10.0f, 12.0f);
        pen.Line(10.0f, 12.0f, 14.5f, 16.5f);
        pen.Line(14.5f, 16.5f, 20.0f, 11.5f);
        break;
    case Icon::Trash:
        pen.Line(4.0f, 7.0f, 20.0f, 7.0f);
        pen.Line(9.5f, 4.0f, 14.5f, 4.0f);
        pen.Line(9.5f, 4.0f, 9.5f, 7.0f);
        pen.Line(14.5f, 4.0f, 14.5f, 7.0f);
        pen.Line(6.0f, 7.0f, 6.8f, 20.0f);
        pen.Line(6.8f, 20.0f, 17.2f, 20.0f);
        pen.Line(17.2f, 20.0f, 18.0f, 7.0f);
        pen.Line(10.0f, 10.5f, 10.3f, 17.0f);
        pen.Line(14.0f, 10.5f, 13.7f, 17.0f);
        break;
    case Icon::Copy:
        pen.Box(8.5f, 8.5f, 11.0f, 11.0f, 2.0f);
        pen.Line(15.5f, 6.0f, 15.5f, 4.5f);
        pen.Line(15.5f, 4.5f, 6.0f, 4.5f);
        pen.Line(6.0f, 4.5f, 6.0f, 14.0f);
        pen.Line(6.0f, 14.0f, 7.5f, 14.0f);
        break;
    case Icon::Clipboard:
        pen.Box(5.0f, 4.5f, 14.0f, 16.0f, 2.0f);
        pen.Box(8.5f, 2.5f, 7.0f, 4.0f, 1.2f);
        pen.Line(8.5f, 11.0f, 15.5f, 11.0f);
        pen.Line(8.5f, 14.5f, 13.0f, 14.5f);
        break;

    // ---- settings ------------------------------------------------------
    case Icon::Settings: {
        // Six-tooth gear approximated by a ring plus radial spokes.
        pen.Circle(12.0f, 12.0f, 4.0f);
        pen.Circle(12.0f, 12.0f, 8.0f);
        for (int i = 0; i < 6; ++i) {
            const f32 a = i * 60.0f * kDeg;
            const f32 cx = std::cos(a) * 9.5f;
            const f32 cy = std::sin(a) * 9.5f;
            pen.Line(12.0f + cx * 0.72f, 12.0f + cy * 0.72f, 12.0f + cx * 1.25f,
                     12.0f + cy * 1.25f);
        }
        break;
    }
    case Icon::Sliders:
        pen.Line(3.5f, 7.0f, 20.5f, 7.0f);
        pen.Line(3.5f, 12.0f, 20.5f, 12.0f);
        pen.Line(3.5f, 17.0f, 20.5f, 17.0f);
        pen.Circle(9.0f, 7.0f, 2.4f);
        pen.Circle(15.0f, 12.0f, 2.4f);
        pen.Circle(7.0f, 17.0f, 2.4f);
        break;
    case Icon::Eye:
        pen.Arc(12.0f, 13.0f, 8.0f, 200.0f, 140.0f);
        pen.Arc(12.0f, 13.0f, 8.0f, 20.0f, 140.0f);
        pen.Circle(12.0f, 12.5f, 2.8f);
        break;
    case Icon::EyeOff:
        pen.Arc(12.0f, 13.0f, 8.0f, 200.0f, 140.0f);
        pen.Arc(12.0f, 13.0f, 8.0f, 20.0f, 140.0f);
        pen.Circle(12.0f, 12.5f, 2.8f);
        pen.Line(4.5f, 4.5f, 19.5f, 19.5f);
        break;
    case Icon::Lock:
        pen.Box(5.0f, 10.5f, 14.0f, 9.5f, 2.0f);
        pen.Arc(12.0f, 10.5f, 4.5f, 180.0f, 180.0f);
        break;
    case Icon::Unlock:
        pen.Box(5.0f, 10.5f, 14.0f, 9.5f, 2.0f);
        pen.Arc(10.5f, 10.5f, 4.5f, 180.0f, 180.0f);
        pen.Arc(16.5f, 10.5f, 4.5f, 180.0f, 90.0f);
        break;
    case Icon::Bell:
        pen.Line(6.5f, 16.0f, 6.5f, 11.0f);
        pen.Arc(12.0f, 10.5f, 5.5f, 180.0f, 180.0f);
        pen.Line(17.5f, 16.0f, 17.5f, 11.0f);
        pen.Line(6.5f, 16.0f, 17.5f, 16.0f);
        pen.Arc(12.0f, 16.0f, 2.6f, 0.0f, 180.0f);
        break;
    case Icon::BellOff:
        pen.Line(6.5f, 16.0f, 6.5f, 11.0f);
        pen.Arc(12.0f, 10.5f, 5.5f, 180.0f, 180.0f);
        pen.Line(17.5f, 16.0f, 17.5f, 11.0f);
        pen.Line(6.5f, 16.0f, 17.5f, 16.0f);
        pen.Arc(12.0f, 16.0f, 2.6f, 0.0f, 180.0f);
        pen.Line(3.5f, 3.5f, 20.5f, 20.5f);
        break;
    case Icon::Power:
        pen.Arc(12.0f, 12.5f, 7.5f, -60.0f, 300.0f);
        pen.Line(12.0f, 4.0f, 12.0f, 11.0f);
        break;
    case Icon::Refresh:
    case Icon::Rotate:
        pen.Arc(12.0f, 12.0f, 7.5f, 40.0f, 280.0f);
        pen.Line(18.6f, 5.6f, 18.2f, 11.0f);
        pen.Line(18.2f, 11.0f, 13.2f, 9.0f);
        break;
    case Icon::Link:
        pen.Arc(9.5f, 12.0f, 4.0f, 270.0f, 180.0f);
        pen.Arc(14.5f, 12.0f, 4.0f, 90.0f, 180.0f);
        pen.Line(9.0f, 12.0f, 15.0f, 12.0f);
        break;
    case Icon::Unlink:
        pen.Arc(9.5f, 12.0f, 4.0f, 270.0f, 180.0f);
        pen.Arc(14.5f, 12.0f, 4.0f, 90.0f, 180.0f);
        pen.Line(9.0f, 12.0f, 11.5f, 12.0f);
        pen.Line(12.5f, 12.0f, 15.0f, 12.0f);
        pen.Line(4.5f, 19.0f, 19.5f, 4.5f);
        break;

    // ---- domain --------------------------------------------------------
    case Icon::User:
        pen.Circle(12.0f, 8.0f, 4.0f);
        pen.Arc(12.0f, 20.5f, 7.5f, 180.0f, 180.0f);
        break;
    case Icon::Users:
        pen.Circle(9.0f, 8.5f, 3.6f);
        pen.Arc(9.0f, 20.0f, 6.8f, 180.0f, 180.0f);
        pen.Arc(17.5f, 8.5f, 2.6f, 270.0f, 180.0f);
        pen.Arc(16.5f, 20.0f, 5.4f, 200.0f, 140.0f);
        break;
    case Icon::Tag: {
        const f32 pts[] = { 4.5f, 4.5f, 11.0f, 4.5f, 20.0f, 13.5f, 13.5f, 20.0f, 4.5f, 11.0f };
        pen.Poly(pts, 5);
        pen.Circle(8.5f, 8.5f, 1.5f);
        break;
    }
    case Icon::Bolt:
        pen.Line(13.5f, 2.5f, 6.0f, 13.0f);
        pen.Line(6.0f, 13.0f, 11.5f, 13.0f);
        pen.Line(11.5f, 13.0f, 10.5f, 21.5f);
        pen.Line(10.5f, 21.5f, 18.0f, 11.0f);
        pen.Line(18.0f, 11.0f, 12.5f, 11.0f);
        pen.Line(12.5f, 11.0f, 13.5f, 2.5f);
        break;
    case Icon::Cube: {
        const f32 pts[] = { 12.0f, 2.5f, 21.0f, 7.0f, 21.0f, 17.0f, 12.0f, 21.5f,
                            3.0f, 17.0f, 3.0f, 7.0f };
        pen.Poly(pts, 6);
        pen.Line(3.0f, 7.0f, 12.0f, 12.0f);
        pen.Line(21.0f, 7.0f, 12.0f, 12.0f);
        pen.Line(12.0f, 12.0f, 12.0f, 21.5f);
        break;
    }
    case Icon::Layers: {
        const f32 pts[] = { 12.0f, 3.0f, 21.0f, 8.0f, 12.0f, 13.0f, 3.0f, 8.0f };
        pen.Poly(pts, 4);
        pen.Line(3.0f, 12.5f, 12.0f, 17.5f);
        pen.Line(21.0f, 12.5f, 12.0f, 17.5f);
        pen.Line(3.0f, 16.5f, 12.0f, 21.5f);
        pen.Line(21.0f, 16.5f, 12.0f, 21.5f);
        break;
    }
    case Icon::Target:
        pen.Circle(12.0f, 12.0f, 8.5f);
        pen.Circle(12.0f, 12.0f, 4.5f);
        pen.Circle(12.0f, 12.0f, 1.0f);
        break;
    case Icon::Crosshair:
        pen.Circle(12.0f, 12.0f, 6.0f);
        pen.Line(12.0f, 2.5f, 12.0f, 7.0f);
        pen.Line(12.0f, 17.0f, 12.0f, 21.5f);
        pen.Line(2.5f, 12.0f, 7.0f, 12.0f);
        pen.Line(17.0f, 12.0f, 21.5f, 12.0f);
        break;
    case Icon::Shield:
        pen.Line(12.0f, 2.5f, 20.0f, 5.5f);
        pen.Line(20.0f, 5.5f, 20.0f, 11.5f);
        pen.Arc(12.0f, 12.0f, 8.2f, 0.0f, 90.0f);
        pen.Arc(12.0f, 12.0f, 8.2f, 90.0f, 90.0f);
        pen.Line(12.0f, 2.5f, 4.0f, 5.5f);
        pen.Line(4.0f, 5.5f, 4.0f, 11.5f);
        break;
    case Icon::Star:
        pen.Star(12.0f, 12.0f, 9.2f, 3.9f, -90.0f);
        break;
    case Icon::Heart: {
        pen.Arc(9.0f, 9.5f, 4.2f, 160.0f, 200.0f);
        pen.Arc(15.0f, 9.5f, 4.2f, 200.0f, 160.0f);
        pen.Line(5.2f, 12.2f, 12.0f, 20.5f);
        pen.Line(12.0f, 20.5f, 18.8f, 12.2f);
        break;
    }
    case Icon::Home:
        pen.Line(3.5f, 11.0f, 12.0f, 3.5f);
        pen.Line(12.0f, 3.5f, 20.5f, 11.0f);
        pen.Line(5.5f, 9.5f, 5.5f, 20.0f);
        pen.Line(5.5f, 20.0f, 18.5f, 20.0f);
        pen.Line(18.5f, 20.0f, 18.5f, 9.5f);
        pen.Line(9.5f, 20.0f, 9.5f, 14.0f);
        pen.Line(9.5f, 14.0f, 14.5f, 14.0f);
        pen.Line(14.5f, 14.0f, 14.5f, 20.0f);
        break;
    case Icon::Grid:
        pen.Box(3.5f, 3.5f, 7.0f, 7.0f, 1.5f);
        pen.Box(13.5f, 3.5f, 7.0f, 7.0f, 1.5f);
        pen.Box(3.5f, 13.5f, 7.0f, 7.0f, 1.5f);
        pen.Box(13.5f, 13.5f, 7.0f, 7.0f, 1.5f);
        break;
    case Icon::List:
        pen.Circle(4.5f, 6.5f, 1.3f);
        pen.Line(9.0f, 6.5f, 20.0f, 6.5f);
        pen.Circle(4.5f, 12.0f, 1.3f);
        pen.Line(9.0f, 12.0f, 20.0f, 12.0f);
        pen.Circle(4.5f, 17.5f, 1.3f);
        pen.Line(9.0f, 17.5f, 20.0f, 17.5f);
        break;
    case Icon::Palette:
        pen.Circle(12.0f, 12.0f, 9.0f);
        pen.Circle(8.0f, 9.5f, 1.3f);
        pen.Circle(12.0f, 7.5f, 1.3f);
        pen.Circle(16.0f, 9.5f, 1.3f);
        pen.Arc(14.5f, 15.5f, 4.5f, 0.0f, 110.0f);
        break;
    case Icon::Sparkle: {
        const f32 sp1[] = { 9.0f, 3.0f, 10.6f, 8.4f, 16.0f, 10.0f, 10.6f, 11.6f,
                            9.0f, 17.0f, 7.4f, 11.6f, 2.0f, 10.0f, 7.4f, 8.4f };
        pen.Poly(sp1, 8);
        const f32 sp2[] = { 17.5f, 14.5f, 18.6f, 18.2f, 22.0f, 19.5f, 18.6f, 20.8f,
                            17.5f, 24.0f, 16.4f, 20.8f, 13.0f, 19.5f, 16.4f, 18.2f };
        pen.Poly(sp2, 8);
        break;
    }
    case Icon::Info:
        pen.Circle(12.0f, 12.0f, 9.0f);
        pen.Line(12.0f, 11.0f, 12.0f, 16.5f);
        pen.Circle(12.0f, 7.5f, 0.9f);
        break;
    case Icon::Warning:
        pen.Line(12.0f, 3.5f, 21.5f, 19.5f);
        pen.Line(21.5f, 19.5f, 2.5f, 19.5f);
        pen.Line(2.5f, 19.5f, 12.0f, 3.5f);
        pen.Line(12.0f, 9.5f, 12.0f, 14.0f);
        pen.Circle(12.0f, 16.8f, 0.9f);
        break;
    case Icon::Error:
        pen.Circle(12.0f, 12.0f, 9.0f);
        pen.Line(8.5f, 8.5f, 15.5f, 15.5f);
        pen.Line(15.5f, 8.5f, 8.5f, 15.5f);
        break;
    case Icon::CheckCircle:
        pen.Circle(12.0f, 12.0f, 9.0f);
        pen.Line(7.5f, 12.0f, 11.0f, 15.5f);
        pen.Line(11.0f, 15.5f, 16.5f, 8.5f);
        break;
    case Icon::Play:
        pen.Line(7.0f, 4.5f, 19.0f, 12.0f);
        pen.Line(19.0f, 12.0f, 7.0f, 19.5f);
        pen.Line(7.0f, 19.5f, 7.0f, 4.5f);
        break;
    case Icon::Pause:
        pen.Line(8.5f, 5.0f, 8.5f, 19.0f);
        pen.Line(15.5f, 5.0f, 15.5f, 19.0f);
        break;
    case Icon::Stop:
        pen.Box(6.0f, 6.0f, 12.0f, 12.0f, 2.0f);
        break;
    case Icon::Expand:
        pen.Line(4.0f, 9.5f, 4.0f, 4.0f);
        pen.Line(4.0f, 4.0f, 9.5f, 4.0f);
        pen.Line(14.5f, 4.0f, 20.0f, 4.0f);
        pen.Line(20.0f, 4.0f, 20.0f, 9.5f);
        pen.Line(20.0f, 14.5f, 20.0f, 20.0f);
        pen.Line(20.0f, 20.0f, 14.5f, 20.0f);
        pen.Line(9.5f, 20.0f, 4.0f, 20.0f);
        pen.Line(4.0f, 20.0f, 4.0f, 14.5f);
        break;
    case Icon::Collapse:
        pen.Line(4.0f, 9.5f, 9.5f, 4.0f);
        pen.Line(20.0f, 9.5f, 14.5f, 4.0f);
        pen.Line(4.0f, 14.5f, 9.5f, 20.0f);
        pen.Line(20.0f, 14.5f, 14.5f, 20.0f);
        break;
    case Icon::Filter:
        pen.Line(3.5f, 5.0f, 20.5f, 5.0f);
        pen.Line(6.5f, 12.0f, 17.5f, 12.0f);
        pen.Line(9.5f, 19.0f, 14.5f, 19.0f);
        break;
    case Icon::SortAscending:
        pen.Line(5.0f, 17.0f, 5.0f, 7.0f);
        pen.Line(5.0f, 7.0f, 2.0f, 10.5f);
        pen.Line(5.0f, 7.0f, 8.0f, 10.5f);
        pen.Line(11.0f, 8.0f, 20.5f, 8.0f);
        pen.Line(11.0f, 13.0f, 18.0f, 13.0f);
        pen.Line(11.0f, 18.0f, 15.5f, 18.0f);
        break;
    case Icon::SortDescending:
        pen.Line(5.0f, 7.0f, 5.0f, 17.0f);
        pen.Line(5.0f, 17.0f, 2.0f, 13.5f);
        pen.Line(5.0f, 17.0f, 8.0f, 13.5f);
        pen.Line(11.0f, 8.0f, 15.5f, 8.0f);
        pen.Line(11.0f, 13.0f, 18.0f, 13.0f);
        pen.Line(11.0f, 18.0f, 20.5f, 18.0f);
        break;

    case Icon::None:
    case Icon::COUNT:
    default:
        break;
    }
}

void DrawIcon(Icon icon, const Rect& box, f32 thickness, const Color& color, f32 alpha) {
    DrawIcon(Context::Get().MutableDrawList(), icon, box, thickness, color, alpha);
}

} // namespace argon::ui