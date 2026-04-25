# P3 / 13 — `ComputeObservedOnGround` is a passthrough

## TL;DR
`GoySDK/GameState.hpp` defines `ComputeObservedOnGround(bool rawOnGround) { return rawOnGround; }`. The function does literally nothing besides pass through its argument. Either implement the intended logic or inline the field access.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/GameState.hpp`
- Lines: **3–10**

```cpp
namespace GoySDK {

inline bool ComputeObservedOnGround(bool rawOnGround) {
    return rawOnGround;
}

}
```

Caller: `BotModule::ReadGameState` line **748**:
```cpp
snap.isOnGround = ComputeObservedOnGround(rawOnGround);
```

## Problem
The function name suggests there should be derivation logic ("observed" on ground might combine raw on-ground with `WorldContact.bHasContact`, `WorldContact.Normal.Z`, jump state, etc., to filter brief contact glitches). The implementation is a stub.

## Why it matters
If the bot ever briefly leaves the ground for one tick (e.g., crossing a small bump), `rawOnGround` flips false, and the action mask immediately switches to "air actions only" (per `ActionMask.hpp:137-141`). For one tick, the bot can't apply ground-only actions like throttle+steer combos.

## Fix

### Option A — Implement debouncing (recommended if the bug above is observable)

Either add per-slot state (last N ticks of `rawOnGround`) and require N consecutive false values to flip "observed" off, or use a more sophisticated check:

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/GameState.hpp`. Find:
```cpp
inline bool ComputeObservedOnGround(bool rawOnGround) {
    return rawOnGround;
}
```

Replace with a snapshot-aware version:
```cpp
/// Heuristic "observed on ground": treat the car as grounded if it's actually on
/// the ground OR if it's in contact with a wall/ceiling whose normal is roughly
/// vertical (turtled flat — should still use ground actions). Filters brief
/// 1-tick suspensions during light bumps.
inline bool ComputeObservedOnGround(bool rawOnGround,
                                    bool hasWorldContact,
                                    float worldContactNormalZ) {
    if (rawOnGround) return true;
    if (hasWorldContact && worldContactNormalZ > 0.85f) return true;
    return false;
}
```

Update the call site `BotModule.cpp:748`:
```cpp
snap.isOnGround = ComputeObservedOnGround(rawOnGround);
```
to:
```cpp
snap.isOnGround = ComputeObservedOnGround(
    rawOnGround,
    snap.rawHasWorldContact,
    snap.rawWorldContactNormal.Z);
```

For multi-tick debouncing, add a per-slot ring buffer in `PlayerBotSlot` and have `ComputeObservedOnGround` take a span of the last N raw values.

### Option B — Inline and delete the helper

If no derivation is intended, the helper adds noise. Edit `BotModule.cpp:748`:
```cpp
snap.isOnGround = ComputeObservedOnGround(rawOnGround);
```
Replace with:
```cpp
snap.isOnGround = rawOnGround;
```

Then delete the entire `GameState.hpp` if it has no other content (verify first):
```bash
cat /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/GameState.hpp
```

If it only contains the passthrough, delete the file and remove its `#include "GameState.hpp"` from `BotModule.cpp:4` (and any other includes).

## Verification

### If Option A
- Build.
- Drive the bot over a series of bumps; confirm with diag logging that `snap.isOnGround` stays `true` across single-tick `rawOnGround` flickers.

### If Option B
- Build. Confirm the file deletion didn't break anything.
- Confirm `grep -rn "GameState.hpp\|ComputeObservedOnGround"` returns no hits.

## Don't do
- Don't leave the stub in place "in case we add logic later." Add the logic now or remove the placeholder.
- Don't make the function `constexpr` to "optimize" — `inline` already does what's needed.

## Related
- This relates to action-mask correctness (which is per-snapshot). If you fix this, also re-test the masking behavior with the new on-ground definition.
