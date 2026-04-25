# P1 / 04 — `joinPressCountdown_` negative branch is unreachable

## TL;DR
`BotModule::TickJoinCountdowns()` contains a two-phase state machine:

- negative value: wait before pressing join,
- positive value: hold the join button,
- zero: idle.

But no code currently sets `joinPressCountdown_[i]` to a negative value. That makes the wait phase and the join-press phase unreachable unless some future code manually seeds it.

Fix this by scheduling a join press whenever a ViGEm splitscreen pad is connected or already available for the newly-created splitscreen controller.

## Where
- State machine: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, `TickJoinCountdowns`, current lines around 422-440.
- Missing trigger: same file, `AddSplitscreen`, current lines around 322-375.
- Tick driver: `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp`, `GUIComponent::Render`, current line around 1244.
- Button implementation: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/ViGemController.cpp`, `PressJoin`, current lines around 135-145, sends `XUSB_GAMEPAD_START`.

## Current State Machine
Current code:

```cpp
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
```

This is a valid state machine. The missing piece is the trigger that sets `joinPressCountdown_[slot]` negative.

## Correct Fix Strategy
When `AddSplitscreen()` creates a new local player and the input method is ViGEm, schedule a join press for that controller ID. Do this whether `ConnectPad(nextId)` had to create a new virtual pad or the pad was already connected.

The “already connected” case matters because:
- slot 0 is connected when switching to ViGEm,
- a previous failed splitscreen flow may leave a pad connected,
- reconnect behavior can change as the feature evolves.

## Step 1 — Add a Helper to Schedule Join Presses
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Add a small helper near `TickJoinCountdowns()`:

```cpp
static constexpr int kJoinPressDelayTicks = 60;
static constexpr int kJoinPressHoldTicks = 60;

static void ScheduleJoinPress(std::array<int, 4>& countdowns, int slotIdx) {
    if (slotIdx < 0 || slotIdx >= static_cast<int>(countdowns.size())) {
        return;
    }
    countdowns[slotIdx] = -kJoinPressDelayTicks;
}
```

If you prefer not to add a free helper, add the constants near the top of `BotModule.cpp` and set the array directly. The helper makes the intent clear.

## Step 2 — Use the Constants in `TickJoinCountdowns`
In `TickJoinCountdowns`, replace the hardcoded `60` transition:

```cpp
joinPressCountdown_[i] = 60;
```

with:

```cpp
joinPressCountdown_[i] = kJoinPressHoldTicks;
```

This keeps the delay and hold durations named in one place.

## Step 3 — Schedule After Splitscreen Player Creation
In `AddSplitscreen`, current ViGEm block:

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
        if (!vigemCtrl_.ConnectPad(nextId)) {
            Console.Error("[GoySDK] Failed to connect ViGEm pad " + std::to_string(nextId) + " for splitscreen input.");
            return;
        }
        Console.Write("[GoySDK] ViGEm pad " + std::to_string(nextId) + " connected for splitscreen input.");
    }

    ScheduleJoinPress(joinPressCountdown_, nextId);
    Console.Write("[GoySDK] Scheduled ViGEm join press for controller " + std::to_string(nextId) + ".");
}
```

This schedules the join press after the in-engine local player is created and after the virtual controller is connected.

## Step 4 — Decide Whether GUI Ticks Are the Right Clock
`TickJoinCountdowns()` is currently called from `GUIComponent::Render()`. That means the 60-tick delay is tied to render/UI frame rate, not guaranteed game ticks. The current feature already has that assumption, so the minimal fix keeps it.

If join timing is flaky after Step 3, move `TickJoinCountdowns()` to the game tick path instead:

- Call it once per frame inside `BotModule::PlayerTickCalled` when `isFirstSlotThisFrame` is true.
- Remove the GUI render call to avoid double ticking.

Do not call it from both GUI render and game tick.

## Step 5 — Confirm `PressJoin` Button Choice
`ViGemController::PressJoin` currently sends:

```cpp
report.wButtons |= XUSB_GAMEPAD_START;
```

Verify in-game whether the splitscreen join action expects Start or A. If Start does not join, change it deliberately in `PressJoin`, not at the call site:

```cpp
report.wButtons |= XUSB_GAMEPAD_A;
```

Keep this as a separate validation step because button mapping can vary by game state and input settings.

## Verification
1. Build `GoySDKCore`.

2. Static check:
   ```bash
   rg -n "joinPressCountdown_\\[[^\\]]+\\] = -" /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp
   ```
   Expected: at least one scheduling write.

3. Manual ViGEm test:
   - Set input method to ViGEm.
   - Call `AddSplitscreen()`.
   - Confirm the console logs the pad connection or existing pad and the scheduled join press.
   - Confirm the new splitscreen player joins without a physical controller press.

4. Already-connected test:
   - Ensure `vigemCtrl_.IsPadConnected(nextId)` is true before `AddSplitscreen`.
   - Confirm join is still scheduled.

5. State-machine test:
   - Temporarily set `joinPressCountdown_[1] = -3`.
   - Call `TickJoinCountdowns()` repeatedly.
   - Confirm it transitions `-3 -> -2 -> -1 -> 60 -> 59 ... -> 0`, sends join while positive, then sends neutral at zero.

## Don't Do
- Do not delete `joinPressCountdown_`; the state machine is useful.
- Do not schedule only inside the `ConnectPad` success branch; existing connected pads need the trigger too.
- Do not tick the countdown from both GUI render and game tick.
- Do not change the delay/hold duration without manual testing; the game may need a short enumeration delay before accepting controller input.

## Related
- **P1/05** — another dead automation-related field cleanup.
