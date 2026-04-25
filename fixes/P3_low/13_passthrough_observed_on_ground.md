# P3 / 13 — `ComputeObservedOnGround` is a passthrough helper

## TL;DR
`GoySDK/GameState.hpp` defines:

```cpp
inline bool ComputeObservedOnGround(bool rawOnGround) {
    return rawOnGround;
}
```

That function adds no behavior. Either delete it and inline `rawOnGround`, or implement a real “observed on ground” heuristic. The safer default is deletion because a ground-state heuristic can change action-mask behavior and should be designed/tested deliberately.

## Where
- Helper: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/GameState.hpp`, current lines around 6-8.
- Caller: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, current line around 748:
  ```cpp
  snap.isOnGround = ComputeObservedOnGround(rawOnGround);
  ```
- Include: `BotModule.cpp`, current line around 4:
  ```cpp
  #include "GameState.hpp"
  ```

## Correct Fix Options
Choose one:

- **Option A, recommended:** inline `rawOnGround` and delete the empty helper/file.
- **Option B:** implement a real heuristic, but only with explicit inputs and regression testing against the action mask.

## Option A — Inline and Delete
### Step A1 — Replace the Call Site
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Replace:

```cpp
snap.isOnGround = ComputeObservedOnGround(rawOnGround);
```

with:

```cpp
snap.isOnGround = rawOnGround;
```

### Step A2 — Remove the Include
In the same file, delete:

```cpp
#include "GameState.hpp"
```

### Step A3 — Delete the Empty File
Delete:

```text
/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/GameState.hpp
```

Only delete it after confirming no other file includes it:

```bash
rg -n "GameState.hpp|ComputeObservedOnGround" /Users/carterbarker/Documents/GoySDK/internal_bot
```

Expected before deletion:
- one include,
- one call,
- the helper definition.

Expected after deletion:
- no matches.

## Option B — Implement a Real Heuristic
Only choose this if you have observed action-mask flicker caused by one-frame `bOnGround` noise.

### Step B1 — Define the Heuristic Clearly
A minimal contact-aware heuristic could be:

```cpp
inline bool ComputeObservedOnGround(
    bool rawOnGround,
    bool hasWorldContact,
    float worldContactNormalZ)
{
    if (rawOnGround) {
        return true;
    }

    return hasWorldContact && worldContactNormalZ > 0.85f;
}
```

This says:
- raw on-ground always wins,
- otherwise strong upward contact counts as ground-like.

### Step B2 — Update the Call Site
Replace:

```cpp
snap.isOnGround = ComputeObservedOnGround(rawOnGround);
```

with:

```cpp
snap.isOnGround = ComputeObservedOnGround(
    rawOnGround,
    snap.rawHasWorldContact,
    snap.rawWorldContactNormal.Z);
```

### Step B3 — Understand the Risk
This changes action-mask behavior in `ActionMask.hpp`:

```cpp
if (player.isOnGround) {
    applyMask(tables.groundMask, true);
} else {
    applyMask(tables.airMask, true);
}
```

A too-permissive ground heuristic can allow ground actions while the car is actually airborne. A too-strict heuristic can keep the existing flicker. Test before shipping.

### Step B4 — Multi-Tick Debounce Requires Slot State
Do not fake debounce inside a stateless inline function. Real debounce needs per-slot history, e.g.:

```cpp
int observedOnGroundGraceTicks = 0;
```

inside `PlayerBotSlot`, updated each frame from `rawOnGround`. That is a larger behavior change and should be a separate P2/P1 fix if action-mask flicker is confirmed.

## Verification
### If Option A
1. Build `GoySDKCore`.
2. Run:
   ```bash
   rg -n "GameState.hpp|ComputeObservedOnGround" /Users/carterbarker/Documents/GoySDK/internal_bot
   ```
   Expected: no matches.
3. Confirm action-mask behavior is unchanged because `snap.isOnGround` still equals `rawOnGround`.

### If Option B
1. Build `GoySDKCore`.
2. Add temporary diagnostics for:
   - `rawOnGround`,
   - `rawHasWorldContact`,
   - `rawWorldContactNormal.Z`,
   - `snap.isOnGround`.
3. Drive over small bumps and wall/ceiling contacts.
4. Confirm the heuristic improves the specific observed problem without allowing obviously wrong ground actions in air.

## Don't Do
- Do not leave a passthrough helper “for later.”
- Do not implement debounce without per-slot state.
- Do not change `isOnGround` semantics without retesting action masks.
- Do not delete `GameState.hpp` before checking for all includes.

## Related
- **P0/05** — action-mask behavior depends on `snap.isOnGround`.
