# P2 / 02 — `AssignModel` doesn't reset humanizer state

## TL;DR
`BotModule::AssignModel` swaps in a new bot for a slot but doesn't reset the slot's `Humanizer`. The smoothing state (`Humanizer::prev_`) carries the last action of the *previous* model into the first ticks of the new one. For the special case `AssignModel(slot, -1)` (set to human), the humanizer is also untouched, so the next time the slot is bot-loaded, it starts from stale prev values.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::AssignModel`
- Lines: **493–518**

`LoadBotForSlot` (the actual loader) does call `slot.humanizer = Humanizer(...)` (line 301), which constructs a fresh humanizer with zeroed `prev_`. So model swap (`AssignModel(slot, modelIdx)` with `modelIdx >= 0`) is correct in the happy path.

The bug is in the `modelIdx == -1` branch (set to human), where the humanizer is left in whatever state the previous bot left it.

```cpp
    if (modelIdx == -1) {
       
        slot.bot.reset();
        slot.obsBuilder.reset();
        slot.modelIdx = -1;
        if (inputMethod_ == InputMethod::ViGem) {
            vigemCtrl_.SendNeutral(slotIdx);
        }
        if (slot.assignedPC) {
            slot.assignedPC->bOverrideInput = 0;
            memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
        }
        Console.Notify("[GoySDK] Slot " + std::to_string(slotIdx) + ": Set to HUMAN control");
        return true;
    }
```

## Problem
1. Setting a slot to human leaves `slot.humanizer.prev_` populated. When the user later switches the same slot back to a bot, the very first humanizer call uses stale `prev_` values from the previous bot, producing a brief incorrect control output.
2. There's also an unused `bool wasActive = botActive_;` (line 517) — flagged separately in **P3/01** but worth bundling cleanup with this fix.

## Why it matters
Subjective "the bot acts weird for half a second after I switch to it" reports. Hard to debug because the state leak is invisible.

## Root cause
`AssignModel(-1)` was added later than `LoadBotForSlot`, and the author copied only the visible cleanup (input override, ViGEm) without the smoothing-state cleanup that's implicit in `LoadBotForSlot`'s humanizer reconstruction.

## Fix

### Step 1 — Reset the humanizer on `AssignModel(-1)`

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find the `if (modelIdx == -1)` block (lines 498–512):

```cpp
    if (modelIdx == -1) {
       
        slot.bot.reset();
        slot.obsBuilder.reset();
        slot.modelIdx = -1;
        if (inputMethod_ == InputMethod::ViGem) {
            vigemCtrl_.SendNeutral(slotIdx);
        }
        if (slot.assignedPC) {
            slot.assignedPC->bOverrideInput = 0;
            memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
        }
        Console.Notify("[GoySDK] Slot " + std::to_string(slotIdx) + ": Set to HUMAN control");
        return true;
    }
```

Replace with:

```cpp
    if (modelIdx == -1) {
        slot.bot.reset();
        slot.obsBuilder.reset();
        slot.modelIdx = -1;
        slot.humanizer.Reset();   // clear smoothing state so a future re-bot starts clean
        slot.lastAction.fill(0.f);
        slot.tickCounter = 0;
        if (inputMethod_ == InputMethod::ViGem) {
            vigemCtrl_.SendNeutral(slotIdx);
        }
        if (slot.assignedPC) {
            slot.assignedPC->bOverrideInput = 0;
            memset(&slot.assignedPC->OverrideInput, 0, sizeof(FVehicleInputs));
        }
        Console.Notify("[GoySDK] Slot " + std::to_string(slotIdx) + ": Set to HUMAN control");
        return true;
    }
```

(`lastAction.fill(0.f)` and `tickCounter = 0` were not explicitly bugged but they're related state the same slot will reuse if/when re-botted; resetting here makes the slot's "off" state truly clean.)

### Step 2 — Drop the unused local variable (P3/01 cross-fix)

In the same function, find (line 517):
```cpp
    bool wasActive = botActive_;
    return LoadBotForSlot(slotIdx, modelIdx);
```

Replace with:
```cpp
    return LoadBotForSlot(slotIdx, modelIdx);
```

## Verification

1. **Build** — `cmake --build`. The unused-variable warning at line 517 should disappear.
2. **Manual swap test** — load a slot, drive briefly, set the slot to human (`AssignModel(0, -1)`), wait a moment, set it back to bot (`AssignModel(0, 0)`). Pre-fix: slight twitch in the first frame. Post-fix: clean start.
3. **Optional instrumentation** — temporarily add `Console.Write("[GoySDK] humanizer prev_[0]=" + std::to_string(slot.humanizer.prev_[0]));` (you'll need to friend `BotModule` to access the private member, or add a temporary getter). Confirm prev_ is zero immediately after `AssignModel(-1)`.

## Don't do

- Do not move the humanizer reset into `Humanizer`'s destructor — it's an aggregated value type, the destructor runs at `BotModule` shutdown only.
- Do not call `slot.humanizer = Humanizer{}` here; you'd lose any non-default constructor args the caller had set. `Reset()` zeroes only the internal smoothing state.
- Do not also reset `slot.config` — the config is owned by the slot and represents user preferences, not per-instance state.

## Related
- **P2/01** — same file, same area. Apply both in the same editing pass.
- **P3/01** — `wasActive` removal is bundled into Step 2 of this fix.
