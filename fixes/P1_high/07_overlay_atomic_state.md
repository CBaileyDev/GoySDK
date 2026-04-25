# P1 / 07 — `IsInGame` and `CurrentGameEvent` are non-atomic across threads

## TL;DR
`OverlayRenderer::IsInGame` (plain `bool`) and `OverlayRenderer::CurrentGameEvent` (raw pointer) are written from the game-thread event hooks (`OnGameEventStart`, `OnGameEventDestroyed`) and read from the render-thread `OnRender` and game-thread `PlayerTickCalled`. The reads at line 209 and 323 are **outside** the `dataMutex` lock, so a concurrent destroy can null the pointer between the check and the dereference, and the `bool` flag has no acquire/release semantics tying it to the pointer publish.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Field declarations: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`
- Field definitions: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp` lines **149–150**
- Unlocked reads: line **209** (`if (!IsInGame || !CurrentGameEvent ...)`), line **323** (`if (!IsInGame) { return; }`)
- Writes: lines **184** (`IsInGame = false;`), **200** (`CurrentGameEvent = static_cast<...>`), **203** (`IsInGame = true;`)

## Problem
1. The line-209 check `if (!IsInGame || !CurrentGameEvent || ...)` happens *before* `std::lock_guard<std::mutex> lock(dataMutex);` at line 213. Between the check and the lock acquisition, another thread can run `OnGameEventDestroyed` which sets `CurrentGameEvent = nullptr`. The function then proceeds to dereference `CurrentGameEvent->LocalPlayers` (line 219) on a null pointer.
2. The plain `bool IsInGame` provides no memory ordering. On weakly-ordered architectures (ARM, but also some Windows-on-ARM scenarios), the writer can publish `IsInGame = true` before `CurrentGameEvent` is visible, so the reader sees `true` and a still-null pointer.
3. The render thread reads `IsInGame` at line 323 with no lock. Same issue: the bool can be `true` while `CurrentGameEvent` and the rendering caches are mid-tear-down.

## Why it matters
Crash on round transitions or exit-to-menu. Especially likely when overlay features (`drawBoostTimers`, `drawBallPrediction`) are enabled, because they iterate cached state under the assumption it's coherent.

## Root cause
The author added `dataMutex` for the data caches but left the gate variables outside the lock for "performance." The cost of an uncontended lock acquisition is nanoseconds, far less than the cost of a use-after-free.

## Fix

### Step 1 — Make the state atomic

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`. Find the declarations of `IsInGame` and `CurrentGameEvent`:

```cpp
static bool IsInGame;
static AGameEvent_Soccar_TA* CurrentGameEvent;
```

Replace with:

```cpp
static std::atomic<bool> IsInGame;
static std::atomic<AGameEvent_Soccar_TA*> CurrentGameEvent;
```

Add `#include <atomic>` near the top of the header if not already present.

### Step 2 — Update the field definitions

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`. Find lines 149–150:

```cpp
bool OverlayRenderer::IsInGame = false;
AGameEvent_Soccar_TA* OverlayRenderer::CurrentGameEvent = nullptr;
```

Replace with:

```cpp
std::atomic<bool> OverlayRenderer::IsInGame{false};
std::atomic<AGameEvent_Soccar_TA*> OverlayRenderer::CurrentGameEvent{nullptr};
```

### Step 3 — Update the writers to use release ordering

In `OnGameEventStart` (line 193), find:
```cpp
            CurrentGameEvent = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
            Console.Write("GameEventHook: Stored GameEvent instance");
        }
        IsInGame = true;
```

Replace with:
```cpp
            CurrentGameEvent.store(static_cast<AGameEvent_Soccar_TA*>(event.Caller()), std::memory_order_release);
            Console.Write("GameEventHook: Stored GameEvent instance");
        }
        IsInGame.store(true, std::memory_order_release);
```

In `OnGameEventDestroyed` (line 178), find:
```cpp
        std::lock_guard<std::mutex> lock(dataMutex);
        CurrentGameEvent = nullptr;
        IsInGame = false;
```

Replace with:
```cpp
        std::lock_guard<std::mutex> lock(dataMutex);
        IsInGame.store(false, std::memory_order_release);
        CurrentGameEvent.store(nullptr, std::memory_order_release);
```

(Order matters: clear `IsInGame` before nulling the pointer so a reader who sees `IsInGame == true` is more likely to also see a non-null pointer. Pair this with the snapshot pattern in Step 4 to make the reader correctness independent of order.)

### Step 4 — Update the readers to snapshot under lock

In `PlayerTickCalled` (line 208), find:
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
    AGameEvent_Soccar_TA* gevt = CurrentGameEvent.load(std::memory_order_acquire);
    if (!IsInGame.load(std::memory_order_acquire) || !gevt) {
        return;
    }
```

Then below that, replace every `CurrentGameEvent->...` with `gevt->...`. Specifically:
- Line 219: `TArray<APlayerController_TA*> localPlayers = CurrentGameEvent->LocalPlayers;` → `TArray<APlayerController_TA*> localPlayers = gevt->LocalPlayers;`
- Line 226: `TArray<ACar_TA*> cars = CurrentGameEvent->Cars;` → `TArray<ACar_TA*> cars = gevt->Cars;`
- Line 227: `TArray<ABall_TA*> balls = CurrentGameEvent->GameBalls;` → `TArray<ABall_TA*> balls = gevt->GameBalls;`

In `OnRender` (line 322), find:
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
    if (!IsInGame.load(std::memory_order_acquire)) {
        return;
    }
```

(`OnRender` doesn't dereference `CurrentGameEvent`, so we only need the bool check, but inside the lock to keep state and caches coherent.)

## Verification

1. **Build** — `cmake --build`.
2. **Stress test** — enable all overlay options (`drawBoostTimers`, `drawBallPrediction`, etc.), play a string of exhibition matches, force-quit each match in different states. Pre-fix: occasional crash in `OverlayRenderer::PlayerTickCalled` or `OnRender`. Post-fix: no crashes.
3. **Static check** — confirm `IsInGame` and `CurrentGameEvent` are accessed only via `.load(...)` / `.store(...)` (no implicit conversions). Run:
   ```bash
   awk '/OverlayRenderer::/,/^}/' /Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp | grep -nE '\b(IsInGame|CurrentGameEvent)\b' | grep -v '\.load\|\.store\|^[0-9]*:OverlayRenderer'
   ```
   Expected: zero hits.

## Don't do

- Do not just take `dataMutex` for the writer block in `OnGameEventStart` and call it done. The render thread might still see torn writes if the bool isn't atomic, because the lock-acquire in `OnRender` doesn't synchronize-with a lock-release in `OnGameEventStart` if the writer doesn't take the lock.
- Do not use `std::memory_order_relaxed` for these accesses. The whole point is acquire/release pairing — relaxed defeats the purpose.
- Do not change `CurrentGameEvent` to a `std::shared_ptr`. The lifetime is owned by the host engine, not by us; wrapping it in a shared_ptr without a custom deleter is a bug, and with one is over-engineered.

## Related
- **P1/01** — same shape (`padStates_` cross-mutex). Same fix pattern (snapshot under lock).
- **P1/03** — same shape on the BotModule side (`gameEvent_`).
