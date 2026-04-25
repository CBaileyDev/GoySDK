# P1 / 04 — `joinPressCountdown_` negative branch is unreachable

## TL;DR
`BotModule::TickJoinCountdowns` checks `joinPressCountdown_[i] < 0` to schedule a "join press" 60 frames later. But no code anywhere in the project ever sets `joinPressCountdown_[i]` to a negative value. The negative branch is dead, and so the entire join-press feature (used to make a virtual controller pad press the "join" button to claim a splitscreen slot) is silently inert.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::TickJoinCountdowns`
- Lines: **422–440**

```cpp
void BotModule::TickJoinCountdowns() {
    for (int i = 0; i < kMaxSlots; i++) {
        if (joinPressCountdown_[i] < 0) {
           
            joinPressCountdown_[i]++;
            if (joinPressCountdown_[i] == 0) {
               
                joinPressCountdown_[i] = 60;
            }
        } else if (joinPressCountdown_[i] > 0) {
           
            vigemCtrl_.PressJoin(i);
            joinPressCountdown_[i]--;
            if (joinPressCountdown_[i] == 0) {
                vigemCtrl_.SendNeutral(i);
            }
        }
    }
}
```

The full set of writes to `joinPressCountdown_` in the codebase:
- Line 73: `joinPressCountdown_ = {};` (zero-init)
- Line 426: `joinPressCountdown_[i]++;` (only inside the unreachable `< 0` branch)
- Line 429: `joinPressCountdown_[i] = 60;` (only inside the unreachable `< 0` branch)
- Line 434: `joinPressCountdown_[i]--;` (decrement of an already-positive value)
- Line 561: `joinPressCountdown_.fill(0);` (cleanup on game destroy)

There is no path from any user action to setting the value negative.

## Why it matters
The expected use case is: when `AddSplitscreen` adds a new local player, schedule a "press join after a frame delay" so the new pad gets registered to that controller. Today, the splitscreen adds the player but never triggers the press. Users using ViGEm input for splitscreen bots have to manually press join on a real controller — defeating the purpose of having ViGEm.

## Root cause
The author intended to add a call site like `joinPressCountdown_[id] = -X;` in `AddSplitscreen` (`/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp:322`) but it was lost or never added.

## Fix

### Step 1 — Schedule a join-press when ViGEm adds a splitscreen pad

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find the ViGEm pad connection block inside `AddSplitscreen` (lines 363–375):

```cpp
    if (inputMethod_ == InputMethod::ViGem) {
        if (!vigemCtrl_.IsInitialized()) {
            if (!vigemCtrl_.Initialize()) {
                Console.Error("[GoySDK] Failed to initialize ViGEm bus");
                return;
            }
        }
        if (!vigemCtrl_.IsPadConnected(nextId)) {
            if (vigemCtrl_.ConnectPad(nextId)) {
                Console.Write("[GoySDK] ViGEm pad " + std::to_string(nextId) + " connected for splitscreen input.");
            }
        }
    }
```

Replace with:

```cpp
    if (inputMethod_ == InputMethod::ViGem) {
        if (!vigemCtrl_.IsInitialized()) {
            if (!vigemCtrl_.Initialize()) {
                Console.Error("[GoySDK] Failed to initialize ViGEm bus");
                return;
            }
        }
        if (!vigemCtrl_.IsPadConnected(nextId)) {
            if (vigemCtrl_.ConnectPad(nextId)) {
                Console.Write("[GoySDK] ViGEm pad " + std::to_string(nextId) + " connected for splitscreen input.");
                // Schedule a "press join" sequence: wait 60 ticks (~0.5s) for the pad
                // to be enumerated by the game, then press join for 60 ticks.
                // TickJoinCountdowns drives the state machine.
                joinPressCountdown_[nextId] = -60;
            }
        }
    }
```

The `-60` value matches the `joinPressCountdown_[i]++` increment loop in `TickJoinCountdowns`: it'll count from -60 up to 0 over 60 ticks, then transition to the +60 "actively pressing" phase.

### Step 2 — Confirm `TickJoinCountdowns` is called from the GUI loop

Already verified — `Components/Components/GUI.cpp:1244` calls `BotMod.TickJoinCountdowns();`. No change needed.

### Step 3 — Make sure `vigemCtrl_.PressJoin(i)` actually does something useful

Open `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/ViGemController.cpp` line 135 and confirm `PressJoin` sends a controller report with the join button (typically `XUSB_GAMEPAD_A` or `XUSB_GAMEPAD_START`). If it's a stub, that's a separate issue but not part of this fix.

## Verification

1. **Build** — `cmake --build`.
2. **Manual test (requires ViGEmBus installed)** — set `inputMethod_ = InputMethod::ViGem`, then call `AddSplitscreen()`. Watch the console: ~0.5s after the "ViGEm pad N connected" log, the join button on virtual controller N should fire (visible in `joy.cpl` if you have it open, or by entering a game and seeing the new player join).
3. **Unit test of state machine** — extract `TickJoinCountdowns` logic into a free function that takes the array by ref and call it in a loop with a manually-set `joinPressCountdown_[0] = -60`. Confirm the state transitions: -60 → … → -1 → 0 → 60 → … → 1 → 0 (with `PressJoin` and `SendNeutral` called appropriately).

## Don't do

- Do not delete the `joinPressCountdown_` array as "dead code." The state machine is correct; what's missing is the one call site that wakes it up.
- Do not change the magic number `60` without testing — it corresponds to ~0.5s at 120Hz which is roughly how long the game needs to enumerate a freshly-connected ViGEm pad.
- Do not move the trigger to `RemoveSplitscreen` or anywhere else. The trigger only makes sense at *connect* time.

## Related
- **P1/05** — `autoRequeue_`/`autoChat_` are similarly inert UI toggles. Same family of bug.
