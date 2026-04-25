# P1 / 07 — Overlay game-event state is read outside its lock

## TL;DR
`OverlayRenderer::IsInGame` and `OverlayRenderer::CurrentGameEvent` are public static fields. They are written by game-event hooks and read by `OverlayRenderer::PlayerTickCalled`, `OverlayRenderer::OnRender`, and `GUIComponent::Render`. Some reads happen before `OverlayRenderer::dataMutex` is acquired.

The old suggested fix used atomics. Atomics alone do not solve the underlying problem because `CurrentGameEvent` points to an Unreal-owned object whose lifetime is not controlled by GoySDK. The better fix is to make overlay state private, guard it with `dataMutex`, have `OnRender` perform its own locked state check, and remove public unlocked reads.

## Where
- State declarations: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`, current lines around 50-51.
- State definitions: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`, current lines around 149-150.
- Unlocked reads:
  - `OverlayRenderer::PlayerTickCalled`, current line around 209.
  - `OverlayRenderer::OnRender`, current line around 323.
  - `GUIComponent::Render`, `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp`, current line around 1242: `if (OverlayMod.IsInGame) OverlayMod.OnRender();`.
- Writes:
  - `OverlayRenderer::OnGameEventStart`, current lines around 193-203.
  - `OverlayRenderer::OnGameEventDestroyed`, current lines around 178-189.

## Correct Fix Strategy
Use `dataMutex` as the single synchronization boundary for overlay state and render caches:

- Keep `CurrentGameEvent` private and accessed only while `dataMutex` is held.
- Keep `IsInGame` private and accessed only while `dataMutex` is held.
- Add a locked public accessor if another component needs to query state.
- Prefer calling `OverlayMod.OnRender()` unconditionally from GUI; `OnRender()` should return early under its own lock.
- Do not rely on `std::atomic<AGameEvent_Soccar_TA*>` for lifetime. Atomic pointer publication still does not keep the Unreal object alive.

## Step 1 — Make State Private and Rename Fields
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`.

Replace the public fields:

```cpp
static bool IsInGame;
static AGameEvent_Soccar_TA* CurrentGameEvent;
```

with a public accessor:

```cpp
static bool IsInGameActive();
```

Then add private fields near the bottom of the class:

```cpp
private:
    static bool isInGame_;
    static AGameEvent_Soccar_TA* currentGameEvent_;
```

If this header currently has no private section, add one after the public configuration/cache declarations. Keep `dataMutex` accessible as needed by existing code, but do not leave `isInGame_` or `currentGameEvent_` public.

## Step 2 — Update Static Definitions
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`.

Replace:

```cpp
bool OverlayRenderer::IsInGame = false;
AGameEvent_Soccar_TA* OverlayRenderer::CurrentGameEvent = nullptr;
```

with:

```cpp
bool OverlayRenderer::isInGame_ = false;
AGameEvent_Soccar_TA* OverlayRenderer::currentGameEvent_ = nullptr;
```

Add the accessor:

```cpp
bool OverlayRenderer::IsInGameActive() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return isInGame_;
}
```

## Step 3 — Update Writers
In `OverlayRenderer::OnGameEventDestroyed`, replace:

```cpp
std::lock_guard<std::mutex> lock(dataMutex);
CurrentGameEvent = nullptr;
IsInGame = false;
carBoostData.clear();
...
```

with:

```cpp
std::lock_guard<std::mutex> lock(dataMutex);
isInGame_ = false;
currentGameEvent_ = nullptr;
localPlayerPRI = nullptr;
carBoostData.clear();
ballScreenPositions.clear();
ballPredictionSamples.clear();
boostTimerBadges.clear();
```

In `OverlayRenderer::OnGameEventStart`, replace:

```cpp
if (event.Caller() && event.Caller()->IsA(AGameEvent_Soccar_TA::StaticClass()))
{
    CurrentGameEvent = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
    Console.Write("GameEventHook: Stored GameEvent instance");
}
IsInGame = true;
```

with:

```cpp
if (event.Caller() && event.Caller()->IsA(AGameEvent_Soccar_TA::StaticClass()))
{
    std::lock_guard<std::mutex> lock(dataMutex);
    currentGameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
    isInGame_ = true;
    Console.Write("GameEventHook: Stored GameEvent instance");
}
```

Do not set `isInGame_ = true` if the caller was not a valid `AGameEvent_Soccar_TA`.

## Step 4 — Update `PlayerTickCalled`
Current start:

```cpp
void OverlayRenderer::PlayerTickCalled(const PostEvent& event) {
    if (!IsInGame || !CurrentGameEvent || !event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) {
        return;
    }

    std::lock_guard<std::mutex> lock(dataMutex);
```

Replace with:

```cpp
void OverlayRenderer::PlayerTickCalled(const PostEvent& event) {
    if (!event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) {
        return;
    }

    std::lock_guard<std::mutex> lock(dataMutex);
    if (!isInGame_ || !currentGameEvent_) {
        return;
    }

    AGameEvent_Soccar_TA* gevt = currentGameEvent_;
```

Then replace:

```cpp
TArray<APlayerController_TA*> localPlayers = CurrentGameEvent->LocalPlayers;
TArray<ACar_TA*> cars = CurrentGameEvent->Cars;
TArray<ABall_TA*> balls = CurrentGameEvent->GameBalls;
```

with:

```cpp
TArray<APlayerController_TA*> localPlayers = gevt->LocalPlayers;
TArray<ACar_TA*> cars = gevt->Cars;
TArray<ABall_TA*> balls = gevt->GameBalls;
```

This still reads an Unreal-owned object, but it eliminates the check-then-lock race and keeps all overlay cache mutations under one lock.

## Step 5 — Update `OnRender`
Current start:

```cpp
void OverlayRenderer::OnRender() {
    if (!IsInGame) {
        return;
    }

    std::lock_guard<std::mutex> lock(dataMutex);
```

Replace with:

```cpp
void OverlayRenderer::OnRender() {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (!isInGame_) {
        return;
    }
```

No caller should need to check `IsInGame` before calling `OnRender`.

## Step 6 — Update GUI Call Site
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp`.

Current code:

```cpp
if (OverlayMod.IsInGame) OverlayMod.OnRender();
```

Replace with:

```cpp
OverlayMod.OnRender();
```

`OnRender` now owns the state check under `dataMutex`.

If you still want a public query for UI display, use:

```cpp
if (OverlayRenderer::IsInGameActive()) {
    ...
}
```

Do not read a public static bool.

## Step 7 — Static Checks
After editing, run:

```bash
rg -n "IsInGame|CurrentGameEvent|isInGame_|currentGameEvent_" /Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.* /Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp
```

Expected:
- No `OverlayMod.IsInGame`.
- No public `CurrentGameEvent`.
- `isInGame_` and `currentGameEvent_` only in `OverlayRenderer.cpp` and private declarations.
- `IsInGameActive()` only as a locked accessor.

## Verification
1. Build `GoySDKCore`.

2. Render smoke test:
   - Inject, enter a match, enable overlay features.
   - Confirm overlays still render.

3. Transition stress:
   - Enter and leave matches repeatedly.
   - Toggle `drawBoostTimers` and `drawBallPrediction`.
   - Confirm no crash in `PlayerTickCalled` or `OnRender`.

4. Destroy-state test:
   - After `OnGameEventDestroyed`, confirm `carBoostData`, `ballScreenPositions`, `ballPredictionSamples`, and `boostTimerBadges` are empty and `OnRender` returns early.

## Don't Do
- Do not make `CurrentGameEvent` atomic and call it solved. Atomic raw pointers do not own engine object lifetime.
- Do not leave `GUI.cpp` reading a public static state flag.
- Do not check state before locking and then mutate caches after locking; that is the exact race being fixed.
- Do not hold `dataMutex` while calling unrelated bot inference or GUI control logic. Keep it scoped to overlay state and render caches.

## Related
- **P1/01** — boost pad state should be copied as a snapshot before overlay reads it.
- **P1/03** — BotModule has a similar game-event pointer issue.
