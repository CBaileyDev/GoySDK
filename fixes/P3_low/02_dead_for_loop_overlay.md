# P3 / 02 — Empty body for-loop in `OverlayRenderer::PlayerTickCalled`

## TL;DR
A `for` loop iterates `localPlayers`, reads `localPlayer->Car` and `localPlayer->PRI` into local pointers, and then does nothing with them. The loop body is empty. Every render frame, this is a wasted pointer chase per local player.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Function: `OverlayRenderer::PlayerTickCalled`
- Lines: **229–235**

```cpp
for (APlayerController_TA* localPlayer : localPlayers) {
   
    ACar_TA* car = localPlayer->Car;
    APRI_TA* PRI = localPlayer->PRI;

   
}
```

## Problem
The loop reads two member fields per element and discards them. There's no side effect. It was probably scaffolding for a feature that wasn't implemented.

## Fix

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`. Find:
```cpp
    for (APlayerController_TA* localPlayer : localPlayers) {
       
        ACar_TA* car = localPlayer->Car;
        APRI_TA* PRI = localPlayer->PRI;

       
    }
```

Replace with: (empty — delete the entire block, including the leading and trailing blank lines around it).

## Verification
1. Build.
2. Inject. Confirm the overlay still renders identically.
3. Optional: profile a frame in `PlayerTickCalled` before and after; the dead loop will show a tiny but measurable cost reduction with many local players.

## Don't do
Don't repurpose the loop "while you're there" — adding a feature that depends on `car` or `PRI` is a separate change. Just remove the dead code.
