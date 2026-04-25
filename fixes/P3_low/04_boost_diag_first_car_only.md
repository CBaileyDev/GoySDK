# P3 / 04 — `boostDiagDone_` diagnostic is one-shot but has no “missing component” signal

## TL;DR
The earlier diagnosis for this note was inaccurate. The current code does **not** print only for the first car in the list. It prints for the first car encountered that has a `BoostComponent`:

```cpp
if (!boostDiagDone_ && boostComp) {
    ...
    boostDiagDone_ = true;
}
```

That behavior is mostly fine. The real low-priority issue is that if no car has a `BoostComponent` on early ticks, the diagnostic stays silent, so you cannot distinguish “diagnostic not reached” from “no boost component found yet.”

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::ReadGameState`
- Diagnostic block around current lines 729-734.
- Field declaration: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`, `boostDiagDone_`.

## Correct Fix Options
Choose one:

- **Option A: Keep the diagnostic and make absence visible.**
- **Option B: Remove the diagnostic entirely if it has served its purpose.**

Do not change the current block under the assumption that it only checks the first car. It already checks every car until one with `BoostComponent` is found.

## Option A — Add a “No BoostComponent Yet” Diagnostic
This is useful if boost component wiring is still being validated.

### Step A1 — Track Whether Any Car Had a Boost Component
In `ReadGameState`, before the car loop:

```cpp
bool sawBoostComponentThisTick = false;
```

Inside the car loop, immediately after:

```cpp
auto* boostComp = car->BoostComponent;
```

add:

```cpp
if (boostComp) {
    sawBoostComponentThisTick = true;
}
```

Keep the existing diagnostic block:

```cpp
if (!boostDiagDone_ && boostComp) {
    Console.Write("[GoySDK] BOOST DIAG: raw=" +
        std::to_string(boostComp->CurrentBoostAmount) +
        " stored=" + std::to_string(snap.boost));
    boostDiagDone_ = true;
}
```

### Step A2 — Print Absence After the Loop
After the `for (int i = 0; i < static_cast<int>(cars.size()); i++)` loop ends, add:

```cpp
if (!boostDiagDone_ && !cars.empty() && !sawBoostComponentThisTick) {
    Console.Write("[GoySDK] BOOST DIAG: no car had a BoostComponent on this tick.");
    boostDiagDone_ = true;
}
```

This keeps the diagnostic one-shot. It prints either:

- one actual raw/stored boost sample, or
- one explicit “no component found” message.

### Step A3 — Reset Behavior
Leave existing resets intact:

- `OnCreate` sets `boostDiagDone_ = false`.
- `OnGameEventDestroyed` sets `boostDiagDone_ = false`.

That gives one diagnostic per match/session reset.

## Option B — Remove the Diagnostic Entirely
This is better if boost reading is already trusted.

Delete the block:

```cpp
if (!boostDiagDone_ && boostComp) {
    Console.Write("[GoySDK] BOOST DIAG: raw=" +
        std::to_string(boostComp->CurrentBoostAmount) +
        " stored=" + std::to_string(snap.boost));
    boostDiagDone_ = true;
}
```

Delete the field declaration in `BotModule.hpp`:

```cpp
static bool boostDiagDone_;
```

Delete the field definition in `BotModule.cpp`:

```cpp
bool BotModule::boostDiagDone_ = false;
```

Delete the resets in:

```cpp
BotModule::OnCreate()
BotModule::OnGameEventDestroyed()
```

## Verification
### If Option A
1. Build `GoySDKCore`.
2. Start a match.
3. Confirm exactly one of these messages appears:
   - `[GoySDK] BOOST DIAG: raw=... stored=...`
   - `[GoySDK] BOOST DIAG: no car had a BoostComponent on this tick.`
4. Leave and re-enter a match; confirm one diagnostic can print again after reset.

### If Option B
1. Build `GoySDKCore`.
2. Run:
   ```bash
   rg -n "boostDiagDone_|BOOST DIAG" /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK
   ```
   Expected: no matches.

## Don't Do
- Do not claim the current code only examines the first car; it does not.
- Do not set `boostDiagDone_ = true` before checking all cars unless you intentionally want the “no component” message.
- Do not leave a dead `boostDiagDone_` field after deleting the diagnostic.

## Related
None.
