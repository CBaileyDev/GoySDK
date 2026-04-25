# P2 / 01 — Humanizer deadzone snaps model output to zero

## TL;DR
`Humanizer::Smooth` applies a deadzone (`fabsf(val) < dead_` → `val = 0.f`) **after** smoothing. The discrete action table emits values from {-1, 0, 1}, so an intentional `+1.0f` from the policy can be exponentially-smoothed to `prev + 0.2 * (1.0 - prev)` and, if the previous output was 0, the first sample is `0.2` — which passes the default `0.08` deadzone. But with `smoothFactor = 0.80f` (the default in the constructor) `alpha = 0.20f` the math is:
- `Smooth(1.0, prev=0)` → `0 + 0.2 * 1 = 0.2` → not deadzoned (0.2 > 0.08) → fine.
- `Smooth(0.5, prev=0)` → `0 + 0.2 * 0.5 = 0.1` → not deadzoned, fine.
- `Smooth(-0.1, prev=0)` → `0 + 0.2 * -0.1 = -0.02` → DEADZONED to 0.

For the discrete action table whose ground-control yaw is in `{-1, 0, 1}`, this is fine for the extremes. But the `pitch` and `roll` channels for **air actions** can produce intentional small magnitudes when the action table is consulted *and* when the policy emits a fractional value (e.g., from the `bo` substituted-as-throttle trick at `ActionMask.hpp:73`). Combined with smoothing, those small intentional values can clip to 0.

The bigger issue: the deadzone exists to mask analog-controller noise around the resting position. The policy outputs **discrete** values; there is no noise to mask. The deadzone only ever degrades intent.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Humanizer.hpp`
- Function: `Humanizer::Smooth`
- Lines: **16–32**

```cpp
float Smooth(float target, float& prev) const {
    float alpha = 1.f - smooth_;
    float val = prev + (target - prev) * alpha;

   
    if (fabsf(val) < dead_) val = 0.f;

   
    if (fabsf(val) > 0.95f && jitter_ > 0.f) {
        val += dist_(rng_) * jitter_;
        if (val >  1.f) val =  1.f;
        if (val < -1.f) val = -1.f;
    }

    prev = val;
    return val;
}
```

Default constructor (line 11): `Humanizer(float smoothFactor = 0.80f, float deadzone = 0.08f, float jitter = 0.015f)`.

Default `Config` (line 102 of `Config.hpp`): `float deadzone = 0.02f;` (more conservative).

## Problem
1. The deadzone applies to the smoothed (and thus already attenuated) value, not the input.
2. The 0.08 default in the constructor and the 0.02 default in `Config` disagree — whichever the caller doesn't specify wins.
3. For discrete-output policies, "deadzone the output" is conceptually incoherent.

## Why it matters
The "humanizer" is enabled by `Config::humanize` (default false). When users *do* enable it (e.g., to make the bot look more human in casual play), they get worse-than-stochastic behavior — small intended yaw/roll inputs get dropped, the bot hesitates on small corrections.

## Root cause
Inverted reasoning: the deadzone makes sense for **input from a real controller** (where ~0.05 magnitude noise is normal). It makes no sense for **output toward a virtual controller** (where the source is a clean discrete signal).

## Fix

### Step 1 — Apply the deadzone to the *target*, not the smoothed value

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Humanizer.hpp`. Find:

```cpp
float Smooth(float target, float& prev) const {
    float alpha = 1.f - smooth_;
    float val = prev + (target - prev) * alpha;

   
    if (fabsf(val) < dead_) val = 0.f;

   
    if (fabsf(val) > 0.95f && jitter_ > 0.f) {
        val += dist_(rng_) * jitter_;
        if (val >  1.f) val =  1.f;
        if (val < -1.f) val = -1.f;
    }

    prev = val;
    return val;
}
```

Replace with:

```cpp
float Smooth(float target, float& prev) const {
    // Deadzone the TARGET, not the smoothed value. The smoothed value can be
    // smaller than `target` after one EMA step, so deadzoning post-smoothing
    // erases small but intentional discrete commands from the policy.
    if (fabsf(target) < dead_) target = 0.f;

    const float alpha = 1.f - smooth_;
    float val = prev + (target - prev) * alpha;

    if (fabsf(val) > 0.95f && jitter_ > 0.f) {
        val += dist_(rng_) * jitter_;
        if (val >  1.f) val =  1.f;
        if (val < -1.f) val = -1.f;
    }

    prev = val;
    return val;
}
```

### Step 2 — Reconcile the default deadzone

There are two defaults in conflict: 0.08 in `Humanizer` constructor (line 11), 0.02 in `Config` (line 102 of Config.hpp). Pick **0.0** — the discrete-output policy genuinely never benefits from a deadzone.

In `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Humanizer.hpp`, change line 11:
```cpp
Humanizer(float smoothFactor = 0.80f, float deadzone = 0.08f, float jitter = 0.015f)
```
to:
```cpp
Humanizer(float smoothFactor = 0.80f, float deadzone = 0.00f, float jitter = 0.015f)
```

In `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Config.hpp`, change line 102:
```cpp
    float deadzone = 0.02f;
```
to:
```cpp
    float deadzone = 0.00f;  // Discrete policy output doesn't benefit from output deadzoning.
```

If a user wants a deadzone for hand-off testing, they can still set it explicitly.

### Step 3 — (Optional) Skip humanizing the binary buttons

`Humanizer::ProcessAnalog` only takes the analog channels (`throttle, steer, pitch, yaw, roll`), so `jump`, `boost`, `handbrake` aren't touched. Good. No change needed.

## Verification

1. **Build** — `cmake --build`.
2. **Behavioral test** — enable `Config::humanize`, give the bot a low-magnitude yaw target (e.g., a small in-air correction), confirm the output reaches the controller. Pre-fix: corrections to ~0.1 magnitude get dropped after smoothing. Post-fix: small corrections survive.
3. **Regression** — disable `humanize`, confirm bot behavior is identical (`ProcessAnalog` is only called when `slot.config.humanize` is true at `BotModule.cpp:941-943`).

## Don't do

- Do not delete the deadzone parameter entirely. Some future caller may want to use the humanizer on actual analog input, where deadzoning makes sense.
- Do not move the deadzone *after* the smoothing AND keep applying it pre-clip. That's the current bug.
- Do not raise `smoothFactor` to compensate (e.g., to 0.9) — that just slows the bot's response without fixing the deadzone semantics.

## Related
- **P2/02** — `Humanizer.Reset()` is not called on model swap. The smoothing state can carry stale values into a freshly-loaded model. Same file.
