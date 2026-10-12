# Animation

Animation is part of the state system rather than a separate timeline. A widget
asks for its animated value each frame and gets a smooth value for free,
because the `Context` stores one small record per widget id and advances it.

This page documents [`include/argonui/Anim/Anim.h`](../../include/argonui/Anim/Anim.h).

## The two animation modes

`AdvanceAnim` runs a scalar in one of two modes, chosen by the `duration`
argument on each call.

**Rate based** (`duration <= 0`). The value approaches the target
exponentially, using `Damp` and the `rate` argument. Higher rate is snappier. The
value never exactly reaches the target, so `AdvanceAnim` snaps it when it comes
within 0.0005.

**Duration based** (`duration > 0`). The value travels from `start` to `target`
over exactly `duration` seconds along an easing curve. On completion the value
is set exactly to `target` and the record is marked inactive.

Both modes keep the value continuous. Switching from one to the other mid-flight
does not pop, because the value is a continuous function of time in either mode.

## `AdvanceAnim`

```cpp
inline f32 AdvanceAnim(AnimEntry& a, f32 target, f32 dt, f32 rate, f32 duration,
                       EasingFn curve);
```

Advances one scalar animation.

**Returns:** the new animated value.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `a` | `AnimEntry&` | | The record to advance, updated in place. |
| `target` | `f32` | | Where the value is heading this frame. |
| `dt` | `f32` | | Seconds since the last call. |
| `rate` | `f32` | | Exponential rate. Ignored when `duration > 0`. Non-positive keeps the record's existing rate. |
| `duration` | `f32` | | Fixed length in seconds. `> 0` selects duration mode, `<= 0` selects rate mode. |
| `curve` | `EasingFn` | | Easing curve. A null pointer keeps the record's existing curve. |

An unseeded record seeds itself to `target` on the first call, so the first
frame shows the target with no animation from zero. That is deliberate: a widget
that appears mid-session should not animate from nothing.

{% hint style="warning" %}
`AdvanceAnim` restarts the ease from the current value whenever the target moves,
rather than easing from where the animation originally started. That is what
makes a target that tracks the pointer (a slider thumb following the cursor, a
hover colour flipping back and forth) behave correctly: the motion is always
relative to where the value actually is, so it never overshoots a moving target.
The cost is that a target that jitters by more than `NearlyEqual`'s tolerance
(0.001) restarts the ease every frame, and a duration animation driven by a
noisy target never completes.
{% endhint %}

{% hint style="info" %}
Because the ease restarts from the current value, switching between duration-based
and rate-based animation mid-flight does not pop. The value is continuous either
way, and `a.active` correctly reflects whether motion is still happening in
either mode.
{% endhint %}

## Records

### `AnimEntry`

A scalar that chases a target every frame, either exponentially (rate) or over a
fixed duration with an easing curve. Whichever is used, the value is continuous.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `value` | `f32` | `0.0f` | The current animated value. |
| `target` | `f32` | `0.0f` | Where it is heading. |
| `start` | `f32` | `0.0f` | Value at the moment the target last changed. |
| `rate` | `f32` | `12.0f` | Exponential approach rate. Higher is snappier. Used when `duration <= 0`. |
| `duration` | `f32` | `0.0f` | When greater than 0, the animation takes exactly this long in seconds and uses `curve` instead of exponential damping. |
| `curve` | `EasingFn` | `EaseCubicOut` | Easing curve for duration mode. |
| `elapsed` | `f32` | `0.0f` | Seconds since the target last changed. |
| `seeded` | `bool` | `false` | False until the first `Seed`, at which point the record has a meaningful value. |
| `active` | `bool` | `false` | True while the value is still moving toward its target. |

```cpp
void Seed(f32 v) {
    value = v;
    target = v;
    start = v;
    elapsed = 0.0f;
    seeded = true;
    active = false;
}
```

Seeds the record to a fixed value and marks it settled. After `Seed(v)` the
value is exactly `v`, the target is `v`, and `active` is false.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `v` | `f32` | | The value to seed to. |

### `AnimVec2Entry`

The two-component counterpart of `AnimEntry`, for animating a position or a
size. It is rate based only; there is no duration or curve.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `value` | `Vec2` | `{0.0f, 0.0f}` | The current animated value. |
| `target` | `Vec2` | `{0.0f, 0.0f}` | Where it is heading. |
| `rate` | `f32` | `12.0f` | Exponential approach rate. Higher is snappier. |
| `seeded` | `bool` | `false` | False until the first `Seed`. |

```cpp
void Seed(const Vec2& v) {
    value = v;
    target = v;
    seeded = true;
}
```

Seeds both components to `v`.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `v` | `const Vec2&` | | The value to seed to. |

`AnimVec2Entry` has no `active` flag and no duration mode, because a vector
animating toward a moving target does not need a completion signal.

## Easing

Every function is `f32 f(f32 t)` where `t` is normalised time in `[0, 1]` and
the result is the eased progress. All of them return exactly 0 at `t = 0` and
exactly 1 at `t = 1`. Most are `inline` and header-only, so they cost nothing
to pass as an `EasingFn`.

### `EaseLinear`

```cpp
inline f32 EaseLinear(f32 t) { return t; }
```

No easing. For progress indicators where a curve would misrepresent the rate.

### Quadratic

```cpp
inline f32 EaseQuadIn(f32 t) { return t * t; }
inline f32 EaseQuadOut(f32 t) { return t * (2.0f - t); }
inline f32 EaseQuadInOut(f32 t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
```

The simplest accelerating and decelerating curves. `In` starts slow, `Out` ends
slow, `InOut` does both.

### Cubic

```cpp
inline f32 EaseCubicIn(f32 t) { return t * t * t; }
inline f32 EaseCubicOut(f32 t) { const f32 f = t - 1.0f; return f * f * f + 1.0f; }
inline f32 EaseCubicInOut(f32 t) {
    return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}
```

`EaseCubicOut` is the default curve for an `AnimEntry`, and the right default for
most UI motion: it decelerates into the target without the overshoot that a
back or elastic curve produces.

### Quartic and quintic

```cpp
inline f32 EaseQuartOut(f32 t) { const f32 f = t - 1.0f; return 1.0f - f * f * f * f; }
inline f32 EaseQuintOut(f32 t) {
    const f32 f = t - 1.0f;
    return 1.0f + f * f * f * f * f;
}
```

Steeper decelerations. Use them for a large element settling, where a cubic
curve is still moving too fast at the halfway point.

### Sine

```cpp
inline f32 EaseSineIn(f32 t) { return 1.0f - std::cos(t * 1.57079632679f); }
inline f32 EaseSineOut(f32 t) { return std::sin(t * 1.57079632679f); }
inline f32 EaseSineInOut(f32 t) { return -0.5f * (std::cos(kPi * t) - 1.0f); }
```

The gentlest curves, closest to linear. `EaseSineInOut` is the right choice for
something that should feel mechanical and uninterested, like a crossfade.

### `EaseExpoOut`

```cpp
inline f32 EaseExpoOut(f32 t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
```

An exponential decay that covers most of its distance in the first fraction of
the duration. `EaseExpoOut(0)` is very close to 0 and the curve is nearly flat
right up to the target, which reads as a snap.

{% hint style="warning" %}
`EaseExpoOut` special-cases `t >= 1.0f` and returns exactly 1. The usual
`1 - pow(2, -10t)` formula slightly undershoots at `t = 1` (it gives about
0.999), which would leave a value a hair short of its target forever in a
duration animation. The guard is why every curve on this page returns exactly 1
at `t = 1`.
{% endhint %}

### `EaseCircOut`

```cpp
inline f32 EaseCircOut(f32 t) {
    const f32 f = t - 1.0f;
    return std::sqrt(Max(0.0f, 1.0f - f * f));
}
```

Circular deceleration. The `Max(0.0f, ...)` guards the square root against a
round-off negative just before `t = 1`, which would otherwise return NaN.

### `EaseBackOut`

```cpp
inline f32 EaseBackOut(f32 t) {
    const f32 c1 = 1.70158f;
    const f32 c3 = c1 + 1.0f;
    const f32 f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}
```

Decelerates, overshoots past 1, then settles back. The standard back-easing
overshoot constant.

{% hint style="info" %}
This is the one curve in the set that returns values above 1. An `AdvanceAnim`
driven by it will visibly overshoot its target and come back, which reads as
physical weight. It is wrong for anything that must not exceed a bound, such as
an alpha or a clipping amount, where the overshoot would be a visible bug.
{% endhint %}

### `EaseElasticOut`

```cpp
inline f32 EaseElasticOut(f32 t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const f32 c4 = (2.0f * kPi) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
```

A damped oscillation: overshoots, oscillates, settles. Guarded at both ends
rather than relying on the formula, because the oscillation swings well outside
[0, 1] and the exact endpoints are easy to get wrong.

### `EaseBounceOut`

```cpp
inline f32 EaseBounceOut(f32 t) {
    const f32 n1 = 7.5625f;
    const f32 d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}
```

Four parabolic arcs that read as a ball bouncing to a stop. Like the elastic
curve, it passes above 1 on the last arc.

### `EasingFn`

```cpp
using EasingFn = f32 (*)(f32);
```

The function pointer type for an easing curve. Every function in this section
converts to it implicitly.

Assign it to an `AnimEntry::curve` to change how a duration animation eases:

```cpp
a.curve = EaseBackOut;
```

An `EasingFn` is a plain function pointer, not `std::function`, so a curve must
be a free function with external or `static` linkage. A lambda with a capture
does not convert.

## Using animation

`Context` owns the records, keyed by a `u32` id you choose per widget. The
widgets use `detail::HoverAnim` and `detail::PressAnim` to get consistent feel:
hover at `StyleVarId::HoverRate`, press at `StyleVarId::PressRate`. Those rates
come from `Motion`, so changing the theme changes the feel of every interactive
widget at once.

```cpp
AnimEntry fade;
const f32 v = AdvanceAnim(fade, shouldShow ? 1.0f : 0.0f, dt,
                          StyleVarId::ModalInRate, 0.16f, EaseCubicOut);
```

Pass the rate and duration from the active style so a themed UI animates at its
own speed rather than at a hardcoded one.

## See also

- [Theme](theme.md) - `Motion` holds the rates and durations fed to animation.
- [Style](style.md) - the `HoverRate` and friends vars.
- [Internal helpers](internal.md) - `HoverAnim` and the other widget animations.