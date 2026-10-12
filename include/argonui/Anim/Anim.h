// ArgonUI - Anim/Anim.h
//
// Easing curves and the per-id animation records Context stores. Animation is
// part of the state system rather than a separate timeline, so a widget gets
// smooth motion for free: it asks for its animated value each frame.
#pragma once

#include "../Core/Types.h"

namespace argon::ui {

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

inline f32 EaseLinear(f32 t) { return t; }

inline f32 EaseQuadIn(f32 t) { return t * t; }
inline f32 EaseQuadOut(f32 t) { return t * (2.0f - t); }
inline f32 EaseQuadInOut(f32 t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }

inline f32 EaseCubicIn(f32 t) { return t * t * t; }
inline f32 EaseCubicOut(f32 t) { const f32 f = t - 1.0f; return f * f * f + 1.0f; }
inline f32 EaseCubicInOut(f32 t) {
    return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

inline f32 EaseQuartOut(f32 t) { const f32 f = t - 1.0f; return 1.0f - f * f * f * f; }
inline f32 EaseQuintOut(f32 t) {
    const f32 f = t - 1.0f;
    return 1.0f + f * f * f * f * f;
}

inline f32 EaseSineIn(f32 t) { return 1.0f - std::cos(t * 1.57079632679f); }
inline f32 EaseSineOut(f32 t) { return std::sin(t * 1.57079632679f); }
inline f32 EaseSineInOut(f32 t) { return -0.5f * (std::cos(kPi * t) - 1.0f); }

inline f32 EaseExpoOut(f32 t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }

inline f32 EaseCircOut(f32 t) {
    const f32 f = t - 1.0f;
    return std::sqrt(Max(0.0f, 1.0f - f * f));
}

inline f32 EaseBackOut(f32 t) {
    const f32 c1 = 1.70158f;
    const f32 c3 = c1 + 1.0f;
    const f32 f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}

inline f32 EaseElasticOut(f32 t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const f32 c4 = (2.0f * kPi) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

inline f32 EaseBounceOut(f32 t) {
    const f32 n1 = 7.5625f;
    const f32 d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

using EasingFn = f32 (*)(f32);

// ---------------------------------------------------------------------------
// Animation records
// ---------------------------------------------------------------------------

/// A scalar that chases a target every frame, either exponentially (rate) or
/// over a fixed duration with an easing curve. Whichever is used, the value is
/// continuous, so switching mid-flight never pops.
struct AnimEntry {
    f32 value = 0.0f;
    f32 target = 0.0f;
    f32 start = 0.0f;   ///< Value at the moment the target last changed.
    /// Exponential approach rate. Higher = snappier. Used when duration <= 0.
    f32 rate = 12.0f;
    /// When > 0, the animation takes exactly this long (seconds) and uses
    /// `curve` instead of exponential damping.
    f32 duration = 0.0f;
    EasingFn curve = EaseCubicOut;
    /// Seconds spent since the target last changed.
    f32 elapsed = 0.0f;
    bool seeded = false;
    bool active = false;

    void Seed(f32 v) {
        value = v;
        target = v;
        start = v;
        elapsed = 0.0f;
        seeded = true;
        active = false;
    }
};

struct AnimVec2Entry {
    Vec2 value{0.0f, 0.0f};
    Vec2 target{0.0f, 0.0f};
    f32 rate = 12.0f;
    bool seeded = false;

    void Seed(const Vec2& v) {
        value = v;
        target = v;
        seeded = true;
    }
};

/// Advances one scalar animation. Returns the new value.
inline f32 AdvanceAnim(AnimEntry& a, f32 target, f32 dt, f32 rate, f32 duration,
                       EasingFn curve) {
    if (!a.seeded) a.Seed(target);

    if (!NearlyEqual(a.target, target)) {
        // The target moved: restart the ease from wherever we are now.
        a.target = target;
        a.start = a.value;
        a.elapsed = 0.0f;
        a.active = true;
    } else {
        a.elapsed += dt;
    }
    if (rate > 0.0f) a.rate = rate;
    if (curve != nullptr) a.curve = curve;

    if (duration > 0.0f) {
        a.duration = duration;
        const f32 t = Clamp01(a.elapsed / duration);
        a.value = Lerp(a.start, a.target, a.curve(t));
        if (t >= 1.0f) {
            a.value = a.target;
            a.active = false;
        }
    } else {
        a.duration = 0.0f;
        a.value = Damp(a.value, a.target, dt, a.rate);
        if (NearlyEqual(a.value, a.target, 0.0005f)) {
            a.value = a.target;
            a.active = false;
        }
    }
    return a.value;
}

} // namespace argon::ui