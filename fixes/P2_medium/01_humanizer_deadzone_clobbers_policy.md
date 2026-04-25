# P2 / 01 — Humanizer deadzone is applied to smoothed output instead of input intent

## TL;DR
`Humanizer::Smooth` applies the deadzone after exponential smoothing:

```cpp
float val = prev + (target - prev) * alpha;
if (fabsf(val) < dead_) val = 0.f;
```

That means the humanizer can erase small transitional outputs created by smoothing even when the target is intentional. This is not catastrophic for the current mostly discrete action table (`-1, 0, 1` analog values), but it is still the wrong semantic layer: a deadzone is for filtering noisy input intent, not for clipping the output of the smoother.

Fix by applying the deadzone to `target` before smoothing, and set the default configured deadzone to `0.0f` unless the user explicitly opts in.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Humanizer.hpp`
- Function: `Humanizer::Smooth`, current lines around 16-32.
- Constructor default: same file, current line around 11.
- User config default: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Config.hpp`, current line around 102.
- Call site: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, `RunSlotInferenceTick`, current lines around 941-943.

## What Is Actually Wrong
The old note overstated this for the current action table. Most table outputs are `-1`, `0`, or `1`, so the default smoothing step often produces values above the current default deadzone.

The real issue is still valid:

1. The humanizer receives bot output, not noisy physical-controller input.
2. Smoothing can turn an intentional non-zero target into a smaller transitional value.
3. Deadzoning after smoothing clips that transitional value and changes the smoothing curve.
4. There are two conflicting defaults: `Humanizer` constructor uses `0.08f`, `Config` uses `0.02f`.

The safest behavior for model output is no deadzone by default.

## Correct Fix Strategy
Apply deadzone to the requested target first, then smooth toward that target:

- If the target itself is tiny, treat it as intentional zero.
- If the target is meaningfully non-zero, preserve the smoother's gradual transition even if the first few smoothed values are small.

## Step 1 — Update `Humanizer::Smooth`
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Humanizer.hpp`.

Replace:

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

with:

```cpp
float Smooth(float target, float& prev) const {
    if (std::fabs(target) < dead_) {
        target = 0.f;
    }

    const float alpha = 1.f - smooth_;
    float val = prev + (target - prev) * alpha;

    if (std::fabs(val) > 0.95f && jitter_ > 0.f) {
        val += dist_(rng_) * jitter_;
        if (val >  1.f) val =  1.f;
        if (val < -1.f) val = -1.f;
    }

    prev = val;
    return val;
}
```

The file already includes `<cmath>`. Prefer `std::fabs` over `fabsf` if the surrounding code compiles cleanly with it; otherwise `fabsf` is acceptable.

## Step 2 — Reconcile Defaults
Edit `Humanizer.hpp`.

Change constructor default:

```cpp
Humanizer(float smoothFactor = 0.80f, float deadzone = 0.08f, float jitter = 0.015f)
```

to:

```cpp
Humanizer(float smoothFactor = 0.80f, float deadzone = 0.00f, float jitter = 0.015f)
```

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/Config.hpp`.

Change:

```cpp
float deadzone = 0.02f;
```

to:

```cpp
float deadzone = 0.00f;
```

If the GUI exposes deadzone as a tuning control, keep the control. This change only changes the default.

## Step 3 — Do Not Touch Binary Buttons
`Humanizer::ProcessAnalog` only modifies:

- throttle,
- steer,
- pitch,
- yaw,
- roll.

It does not modify:

- jump,
- boost,
- handbrake.

Leave that boundary intact.

## Step 4 — Pair With State Reset
Apply **P2/02** in the same pass. Changing smoothing semantics does not fix stale `prev_` state when a slot is switched back to human/bot. The reset fix is separate and still needed.

## Verification
1. Build `GoySDKCore`.

2. Unit-style check:
   - Configure `Humanizer h(0.80f, 0.08f, 0.0f)`.
   - Start with `prev = 0`.
   - `Smooth(1.0f, prev)` should return `0.2f`.
   - `Smooth(0.05f, prev)` should target zero because the target is inside deadzone.

3. Default-config check:
   - Confirm `Config.deadzone` starts at `0.0f`.
   - Confirm `Humanizer()` starts with `dead_ == 0.0f`.

4. Runtime check:
   - Enable humanize.
   - Confirm analog outputs ramp smoothly instead of staying at zero until the smoothed value crosses the deadzone threshold.

5. Regression check:
   - Disable humanize.
   - Confirm bot controls are unchanged because `ProcessAnalog` is skipped.

## Don't Do
- Do not delete the deadzone parameter entirely. It may still be useful as an explicit user tuning control.
- Do not apply deadzone both before and after smoothing.
- Do not raise smoothing to hide deadzone artifacts. That makes response slower without fixing semantics.
- Do not humanize binary buttons in this fix.

## Related
- **P2/02** — reset humanizer state on slot/model transitions.
- **P0/05** — masked action selection should be verified before judging humanizer behavior.
