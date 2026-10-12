// ArgonUI - Core/Types.h
// Fundamental scalar aliases, vectors and small math helpers.
// ArgonUI is backend agnostic: nothing in this header may include a GPU SDK header.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace argon::ui {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kEpsilon = 1e-6f;

// Vec2
struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 vx, f32 vy) : x(vx), y(vy) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    constexpr Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2 operator/(f32 s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(f32 s) { x *= s; y *= s; return *this; }

    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }
};

inline constexpr Vec2 operator*(f32 s, const Vec2& v) { return {v.x * s, v.y * s}; }

// Vec4
struct Vec4 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 vx, f32 vy, f32 vz, f32 vw) : x(vx), y(vy), z(vz), w(vw) {}
    explicit constexpr Vec4(const Vec2& xy, f32 vz = 0.0f, f32 vw = 0.0f) : x(xy.x), y(xy.y), z(vz), w(vw) {}

    constexpr Vec2 xy() const { return {x, y}; }
};

// Scalar helpers
template <typename T>
inline constexpr T Min(T a, T b) { return a < b ? a : b; }
template <typename T>
inline constexpr T Max(T a, T b) { return a > b ? a : b; }
template <typename T>
inline constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T>
inline constexpr T Lerp(T a, T b, f32 t) { return static_cast<T>(a + (b - a) * t); }

inline f32 Clamp01(f32 v) { return Clamp(v, 0.0f, 1.0f); }
inline f32 Sign(f32 v) { return v < 0.0f ? -1.0f : 1.0f; }

/// Frame-rate independent exponential smoothing factor.
/// rate ~= how much of the gap is closed per second (e.g. 12 == snappy).
inline f32 DampFactor(f32 dt, f32 rate) {
    return 1.0f - std::exp(-rate * dt);
}

inline f32 Damp(f32 current, f32 target, f32 dt, f32 rate) {
    return Lerp(current, target, DampFactor(dt, rate));
}

inline bool NearlyEqual(f32 a, f32 b, f32 eps = 0.001f) { return std::fabs(a - b) <= eps; }

} // namespace argon::ui
