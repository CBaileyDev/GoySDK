# P3 / 04 — `boostDiagDone_` printed only for first car with boost component

## TL;DR
`BotModule::boostDiagDone_` is set to `true` after the first successful boost-component log, but the gate is inside the per-car loop. If the first car in the iteration has no `BoostComponent` (e.g., demolished, mid-spawn), the flag is never set and the diag is never printed.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::ReadGameState`
- Lines: **728–734**

```cpp
       
        if (!boostDiagDone_ && boostComp) {
            Console.Write("[GoySDK] BOOST DIAG: raw=" +
                std::to_string(boostComp->CurrentBoostAmount) +
                " stored=" + std::to_string(snap.boost));
            boostDiagDone_ = true;
        }
```

## Problem
The intent is "print boost diagnostics once per session." The execution is "print once if the first car you iterate has a boost component." For most matches this works. For a match that starts mid-replay or with a demoed car at index 0, the diag is silently skipped.

## Why it matters
This is a debug aid. When it goes missing, debugging boost-related issues becomes harder. Low priority, but easy to fix.

## Root cause
Author put the gate inside the per-car loop instead of after iterating all cars.

## Fix

### Option A — Print on the first car that has a boost component (hold the loop's invariant)
The current code already does this; the issue is *when no car ever has one*. The fix is that this is acceptable — without a `BoostComponent`, there's nothing to diag. Add an outer check that prints "no boost components found" so the absence is visible:

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find:

```cpp
       
        if (!boostDiagDone_ && boostComp) {
            Console.Write("[GoySDK] BOOST DIAG: raw=" +
                std::to_string(boostComp->CurrentBoostAmount) +
                " stored=" + std::to_string(snap.boost));
            boostDiagDone_ = true;
        }
```

(No change to that block.)

After the `for (int i = 0; ... cars[i] ...)` loop closes (around line 770), add:

```cpp
    if (!boostDiagDone_ && !cars.empty()) {
        Console.Write("[GoySDK] BOOST DIAG: no car had a BoostComponent on this tick.");
        boostDiagDone_ = true;
    }
```

### Option B — Remove the diag entirely
If the diag is no longer useful (the boost-component path has been validated), delete the block:

```cpp
       
        if (!boostDiagDone_ && boostComp) {
            Console.Write("[GoySDK] BOOST DIAG: raw=" +
                std::to_string(boostComp->CurrentBoostAmount) +
                " stored=" + std::to_string(snap.boost));
            boostDiagDone_ = true;
        }
```

Delete this entirely. Also delete the field declaration in `BotModule.hpp:136` (`static bool boostDiagDone_;`) and the definition in `BotModule.cpp:70` (`bool BotModule::boostDiagDone_ = false;`), and the resets at lines 204 and 551.

## Verification

### If Option A
Inject in a fresh match, confirm one of two diag lines fires within the first few ticks.

### If Option B
Build with `-Werror=unused-private-field` and confirm no warnings about `boostDiagDone_`.

## Don't do
Don't change the gate to `if (boostComp || true)` — that breaks the once-per-session invariant.

## Related
None.
