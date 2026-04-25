# P1 / 01 — `padStates_` written under one mutex, read under a different mutex

## TL;DR
`BotModule::padStates_` (a 34-element array of `BoostPadState`) is written by `BotModule::ReadBoostPads` under `BotModule::guiMutex_`. It's read by `OverlayRenderer::PlayerTickCalled` (line 303) via `BotMod.GetPadStates()`, but the overlay holds `OverlayRenderer::dataMutex` — a **different** mutex. There is no synchronization between the writer and reader.

## Where
- Writer file: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Writer lock acquisition: line **615** (`std::lock_guard<std::mutex> lock(guiMutex_);`)
- Writer code: `ReadBoostPads`, lines 773–816 (writes `padStates_[i].available` and `padStates_[i].timer`)
- Reader file: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Reader lock: line **213** (`std::lock_guard<std::mutex> lock(dataMutex);`)
- Reader access: line **303** (`const auto& padStates = BotMod.GetPadStates();`)
- Accessor file: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`
- Accessor: line **166** (`static const std::array<BoostPadState, 34>& GetPadStates() { return padStates_; }`)

## Problem
`BoostPadState` contains a `bool available` and a `float timer`. A torn read on `float` is technically possible on x86 only when crossing cache lines, but on x64 the typical alignment makes it word-atomic in practice. Either way, the **logical** consistency between `available` and `timer` for a given pad index is not guaranteed: the writer can update `available = false` (line 803), then the reader runs, then the writer updates `timer` (line 812). The reader sees a half-updated state.

Beyond tearing, the bigger risk is the `for (int i = 0; i < 34; i++) padStates_[i].available = true;` reset at line 777 — between this loop and the per-pad loop body, the reader sees all pads as available even though the writer is mid-pass.

## Why it matters
The boost-timer overlay flickers, shows wrong respawn times, or briefly draws a "5s" badge over a pad that was actually just picked up. Worst case is purely cosmetic (overlay glitches), but on architectures where `bool` and `float` writes can be reordered, the rendering thread can crash on a `nan` timer.

## Root cause
Two independent components (`BotModule` and `OverlayRenderer`) reach into the same shared state. They each grew their own mutex for their own internal data, and `padStates_` slipped through the cracks.

## Fix

### Step 1 — Make `GetPadStates()` return a snapshot taken under the writer's lock

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`. Find:
```cpp
    static const std::array<BoostPadState, 34>& GetPadStates() { return padStates_; }
```

Replace with:
```cpp
    /// Returns a snapshot of the boost pad states, copied under guiMutex_.
    /// The snapshot is safe to read from any thread.
    static std::array<BoostPadState, 34> GetPadStates();
```

### Step 2 — Implement the snapshot in BotModule.cpp

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Add this function near the other `BotModule::` definitions (e.g., right after `BotModule::IsCudaInferenceAvailable` at line 134, or just before `void BotModule::ReadBoostPads()` at line 773):

```cpp
std::array<BoostPadState, 34> BotModule::GetPadStates() {
    std::lock_guard<std::mutex> lock(guiMutex_);
    return padStates_;  // copy under lock
}
```

### Step 3 — Update the OverlayRenderer call site

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`. Find (line 303):
```cpp
        const auto& padStates = BotMod.GetPadStates();
```

Replace with:
```cpp
        const std::array<BoostPadState, 34> padStates = BotMod.GetPadStates();  // copied snapshot
```

(The `auto&` was binding to the reference returned by the old accessor. The new accessor returns by value, so changing to a value-typed local makes the change explicit and removes any stale-reference risk.)

### Step 4 — Audit any other consumers of `GetPadStates`

```bash
grep -rn "GetPadStates" /Users/carterbarker/Documents/GoySDK/internal_bot/
```

If any other consumer holds a reference across multiple ticks, replace with a fresh snapshot each tick.

## Verification

1. **Build** — `cmake --build` the internal_bot project. Fix any references to the old `const std::array<BoostPadState, 34>&` return type.
2. **Threaded stress test (manual)** — inject the bot, enable the boost-timer overlay (`drawBoostTimers = true`), drive around a 3v3 game where many pads are getting picked up. Watch the overlay for at least 2 minutes. Pre-fix: timers occasionally jump or display impossible values. Post-fix: smooth.
3. **Static check (optional)** — TSan / Helgrind don't run on the game DLL easily, so this is qualitative only. If you have a CI job that runs `RunSlotInferenceTick` and `OverlayRenderer::PlayerTickCalled` from a fuzz harness, run it with TSan and confirm the warning on `padStates_` access is gone.

## Don't do

- Do not change `padStates_` type to `std::array<std::atomic<...>, 34>`. `BoostPadState` is a struct — atomic on a struct requires `std::atomic<BoostPadState>` which is locked emulation anyway and provides worse semantics than the snapshot.
- Do not just add `std::lock_guard<std::mutex> lock(BotModule::LockGuiState());` at the call site. `LockGuiState()` exists (line 66 in BotModule.hpp) and would work, but it leaks the mutex out of BotModule's encapsulation. The snapshot pattern is cleaner.
- Do not unify `dataMutex` and `guiMutex_` into a single global lock. They protect different data sets and combining them would inflate contention.

## Related
- **P1/07** — `IsInGame` and `CurrentGameEvent` have the same class of issue. The fix pattern (snapshot under lock) is reusable.
