# P3 / 01 — Unused `bool wasActive` in `AssignModel`

## TL;DR
Local variable declared, never read. Pure dead code.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Line: **517**

## Problem
```cpp
    bool wasActive = botActive_;
    return LoadBotForSlot(slotIdx, modelIdx);
```

Either the author intended to restore `botActive_` after `LoadBotForSlot` returns and forgot, or the line was a debugging vestige. There is no path that reads `wasActive`.

## Fix

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find:
```cpp
    bool wasActive = botActive_;
    return LoadBotForSlot(slotIdx, modelIdx);
```

Replace with:
```cpp
    return LoadBotForSlot(slotIdx, modelIdx);
```

## Verification
Build with `-Werror=unused-variable` (or MSVC `/W4`) and confirm the warning is gone.

## Don't do
Don't "fix" by adding `(void)wasActive;` — the value isn't doing anything; suppress the symptom by removing the cause.

## Related
- This is bundled into **P2/02** Step 2 if you're applying that fix anyway.
