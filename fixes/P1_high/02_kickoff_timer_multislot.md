# P1 / 02 — Kickoff timer ticks once per slot, runs N× too fast

## TL;DR
`BotModule::isKickoff_` and `BotModule::kickoffTimer_` are static and updated inside `BotModule::RunSlotInferenceTick`, which is called **once per active bot slot per tick**. With 1 active bot the timer is correct; with N active bots it advances N× per game tick, so the `kickoffTimer_ < 2.5f` gate elapses after `2.5/N` seconds of game time.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::RunSlotInferenceTick`
- Lines: **820–841** (the kickoff detection + timer increment block)
- Static state declarations: lines **67–68**

Existing code (lines 833–845):
```cpp
    bool wasKickoff = isKickoff_;
    isKickoff_ = (ballXY < 100.0f) && (ballSpeed < 50.0f);

    if (isKickoff_ && !wasKickoff) {
        kickoffTimer_ = 0.0f;
    }
    if (isKickoff_) {
        kickoffTimer_ += 1.0f / 120.0f;
    }

    int effectiveTickSkip = (isKickoff_ && kickoffTimer_ < 2.5f)
                            ? slot.config.kickoffTickSkip
                            : slot.config.tickSkip;
```

## Problem
The `kickoffTimer_ += 1.0f / 120.0f` runs once per slot per tick. Game ticks happen at ~120Hz, so for N active slots the timer advances by `N / 120` per game tick. Two consequences:

1. With 4 active bots, the `< 2.5f` window expires after `2.5 / 4 = 0.625` seconds of real game time. The `kickoffTickSkip` (faster inference during kickoff) is in effect for less than half a second.
2. `isKickoff_` is reset to `false` and back to `true` based on ball state; if multiple slots see different ball states (they shouldn't, since `ballSnapshot_` is global, but the static is updated mid-frame), the `wasKickoff` edge detection produces redundant resets to `0.0f`.

## Why it matters
The kickoff handling is supposed to give the bot a faster decision rate at the start of a play. With multi-bot scenarios it stops doing that prematurely. The user-visible symptom is "the bot reacts slowly off the kickoff" or "the bot takes the wrong line off the spawn."

## Root cause
The kickoff-tracking state was added when the bot was single-slot. It was never revisited when the multi-slot loop was added. The static variables should have moved to the per-frame block (`isFirstSlotThisFrame == true`), not the per-slot block.

## Fix

The kickoff state is intrinsically per-frame, not per-slot. Update it exactly once per game tick, in the `if (isFirstSlotThisFrame)` block of `PlayerTickCalled`.

### Step 1 — Move the kickoff update out of `RunSlotInferenceTick`

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. In `RunSlotInferenceTick` (around lines 826–841), find:
```cpp
   
    const float ballXY = sqrtf(ballSnapshot_.pos.X * ballSnapshot_.pos.X +
                                ballSnapshot_.pos.Y * ballSnapshot_.pos.Y);
    const float ballSpeed = sqrtf(ballSnapshot_.vel.X * ballSnapshot_.vel.X +
                                   ballSnapshot_.vel.Y * ballSnapshot_.vel.Y +
                                   ballSnapshot_.vel.Z * ballSnapshot_.vel.Z);

    bool wasKickoff = isKickoff_;
    isKickoff_ = (ballXY < 100.0f) && (ballSpeed < 50.0f);

    if (isKickoff_ && !wasKickoff) {
        kickoffTimer_ = 0.0f;
    }
    if (isKickoff_) {
        kickoffTimer_ += 1.0f / 120.0f;
    }

    int effectiveTickSkip = (isKickoff_ && kickoffTimer_ < 2.5f)
                            ? slot.config.kickoffTickSkip
                            : slot.config.tickSkip;
```

Replace with (read-only consumption of the per-frame state):
```cpp
    int effectiveTickSkip = (isKickoff_ && kickoffTimer_ < 2.5f)
                            ? slot.config.kickoffTickSkip
                            : slot.config.tickSkip;
```

### Step 2 — Add the per-frame update inside `PlayerTickCalled`

In `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, find the `isFirstSlotThisFrame` block (lines 608–621):
```cpp
        static int frameCounter = 0;
        bool isFirstSlotThisFrame = false;
        if (playerSlots_[0].assignedPC == pc || lastFrameTickCount_ == -1) {
            frameCounter++;
            if (lastFrameTickCount_ != frameCounter) {
                lastFrameTickCount_ = frameCounter;
                isFirstSlotThisFrame = true;
                {
                    std::lock_guard<std::mutex> lock(guiMutex_);
                    SyncLocalPlayers();
                    ReadGameState(pc);
                    ReadBoostPads();
                }
            }
        }
```

Add a new statement immediately after the `ReadBoostPads();` line (still inside the lock):

```cpp
                {
                    std::lock_guard<std::mutex> lock(guiMutex_);
                    SyncLocalPlayers();
                    ReadGameState(pc);
                    ReadBoostPads();

                    // Per-frame kickoff state. Must run exactly once per game tick;
                    // RunSlotInferenceTick (called per slot) used to do this and ran
                    // it N× too fast.
                    const float ballXY = sqrtf(ballSnapshot_.pos.X * ballSnapshot_.pos.X +
                                               ballSnapshot_.pos.Y * ballSnapshot_.pos.Y);
                    const float ballSpeed = sqrtf(ballSnapshot_.vel.X * ballSnapshot_.vel.X +
                                                  ballSnapshot_.vel.Y * ballSnapshot_.vel.Y +
                                                  ballSnapshot_.vel.Z * ballSnapshot_.vel.Z);
                    const bool wasKickoff = isKickoff_;
                    isKickoff_ = (ballXY < 100.0f) && (ballSpeed < 50.0f);
                    if (isKickoff_ && !wasKickoff) {
                        kickoffTimer_ = 0.0f;
                    }
                    if (isKickoff_) {
                        kickoffTimer_ += 1.0f / 120.0f;
                    }
                }
```

(Replace the existing inner `{ std::lock_guard ... }` block with the version above. The added code goes inside the same lock so it sees a consistent ball snapshot.)

### Step 3 — Reset on game-event destroy (already partially done)

Verify `OnGameEventDestroyed` resets both: lines 552–553 already have `isKickoff_ = false; kickoffTimer_ = 0.0f;`. No change needed.

## Verification

1. **Build** — `cmake --build`.
2. **Single-bot regression** — drive a 1v1 with one bot, kickoff. The diag log inside `RunSlotInferenceTick` should still show a faster `tickCounter` advance for ~2.5 seconds after the round starts, then settle to normal.
3. **Multi-bot regression** — start a 4-bot freeplay (`AddSplitscreen` 3 times, assign all 4 slots). Confirm the kickoff window persists for the same 2.5 seconds as the single-bot case.
4. **Instrumentation (optional)** — add a `Console.Write("kickoffTimer=" + std::to_string(kickoffTimer_));` inside the new per-frame block for one round. Confirm increments are `0.00833…` per frame (~`1/120`) regardless of slot count.

## Don't do

- Do not move only the increment to per-frame and leave the `wasKickoff` detection in per-slot. The `wasKickoff` edge has to be evaluated against the same per-frame `isKickoff_` value or you'll get false positive resets when slots disagree.
- Do not gate the per-frame update on `slot.bot != nullptr`. The kickoff state is global to the match and should update even when no bots are loaded (so the next `LoadBotForSlot` sees correct state).
- Do not move `isKickoff_`/`kickoffTimer_` to per-slot members. They are properties of the match, not the slot.

## Related
- **P0/02** + this fix together remove two reasons the bot looks worse than its training-time logs. Pair them in regression testing.
