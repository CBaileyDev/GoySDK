# P1 / 03 — `gameEvent_` deref without revalidation in auto-skip / auto-forfeit

## TL;DR
Inside `BotModule::PlayerTickCalled`, the `IsGameEventValid()` check at the top guards the early call sites, but the auto-skip-replay and auto-forfeit blocks dereference `gameEvent_` again **without rechecking**. If `OnGameEventDestroyed` runs between the top-of-function check and these later dereferences (cross-thread or via re-entrance through a hooked event), the dereferences are use-after-free.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::PlayerTickCalled`
- Top-of-function guard: line **599** (`if (!IsGameEventValid()) return;`)
- Unguarded dereferences: lines **629** (`gameEvent_->IsInReplayPlayback()`), **642** (`gameEvent_->bCanVoteToForfeit`), **645** (`gameEvent_->GameStateTimeRemaining`), **646** (`gameEvent_->Teams`)

## Problem
`OnGameEventDestroyed` (line 545) sets `gameEvent_ = nullptr` and resets state. It is invoked from a UE3 event hook that is **not guaranteed** to run on the same thread as `PlayerTickCalled`. Even if it usually does, the hook framework can in principle dispatch on a worker thread when the game performs an asynchronous tear-down (round end, exit-to-menu).

Between the line-599 guard and line-629 dereference there are ~30 lines of code that can take an arbitrary amount of time. The window is large enough to matter.

The auto-forfeit block compounds the issue: it touches `localPRI->Team`, `teams[myTeamIdx]->Score`, etc. — pointers obtained *through* `gameEvent_`. If `gameEvent_` is destroyed, those subordinate pointers may be too.

## Why it matters
- Crash on round transitions when auto-forfeit or auto-skip-replay is enabled.
- Difficult-to-reproduce: race window is small.
- The crash will look like "AV during PlayerTickCalled" with no obvious cause.

## Root cause
`IsGameEventValid()` was added to guard *some* of the dereferences. The author missed that the auto-skip and auto-forfeit blocks also need the same guard, because they were added later.

## Fix

### Step 1 — Snapshot `gameEvent_` once at the top, work on the local copy

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find `BotModule::PlayerTickCalled` (line 598). The current top of the function (lines 598–605):

```cpp
void BotModule::PlayerTickCalled(const PostEvent& event) {
    if (!IsGameEventValid()) return;
    if (!event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) return;

    auto* pc = static_cast<APlayerController_TA*>(event.Caller());
    if (!pc) return;

    try {
```

Replace with:

```cpp
void BotModule::PlayerTickCalled(const PostEvent& event) {
    if (!IsGameEventValid()) return;
    if (!event.Caller() || !event.Caller()->IsA(APlayerController_TA::StaticClass())) return;

    auto* pc = static_cast<APlayerController_TA*>(event.Caller());
    if (!pc) return;

    // Snapshot the game event pointer under the GUI mutex so the destroy
    // handler (which clears gameEvent_) can't yank it out from under us
    // mid-tick. We work on `gevt` for the rest of the function.
    AGameEvent_Soccar_TA* gevt = nullptr;
    {
        std::lock_guard<std::mutex> lock(guiMutex_);
        gevt = gameEvent_;
    }
    if (!gevt) return;

    try {
```

### Step 2 — Replace each `gameEvent_->` inside this function with `gevt->`

In `PlayerTickCalled`, find the auto-skip-replay block (lines 627–639):
```cpp
            if (autoSkipReplay_ && skipReplayCooldown_ <= 0) {
                try {
                    if (gameEvent_->IsInReplayPlayback()) {
                        pc->bOverrideInput = 1;
                        pc->OverrideInput.bJump = 1;
                        skipReplayCooldown_ = 30;
                    }
                } catch (const std::exception& e) {
                    Console.Error(std::string("[GoySDK] Exception in auto-skip replay: ") + e.what());
                } catch (...) {
                    Console.Error("[GoySDK] Unknown exception in auto-skip replay");
                }
            }
```

Replace `gameEvent_->IsInReplayPlayback()` with `gevt->IsInReplayPlayback()`.

Then find the auto-forfeit block (lines 642–666):
```cpp
            if (autoForfeit_ && !forfeitVotedThisMatch_ && gameEvent_->bCanVoteToForfeit) {
                try {
                    APRI_TA* localPRI = pc->PRI;
                    int timeRemaining = gameEvent_->GameStateTimeRemaining;
                    auto teams = gameEvent_->Teams;
                    ...
```

Replace each `gameEvent_->` with `gevt->`.

### Step 3 — Same treatment for `ReadGameState` and `SyncLocalPlayers`

`ReadGameState` (line 694) reads `gameEvent_->GameBalls` and `gameEvent_->Cars` after its own `IsGameEventValid()` check (line 696). The check is closer to the use, so the window is small but non-zero.

Apply the same pattern: snapshot `gameEvent_` into a local at the top of the function, then use the local. The function is called under `guiMutex_` already (from `PlayerTickCalled` line 615), so the snapshot can use `gameEvent_` directly without re-locking — but you should still create a local for clarity:

```cpp
void BotModule::ReadGameState(APlayerController_TA* anyPC) {
    (void)anyPC;
    if (!IsGameEventValid()) return;
    AGameEvent_Soccar_TA* gevt = gameEvent_;  // already under guiMutex_
    if (!gevt) return;
    allPlayers_.clear();

    TArray<ABall_TA*> balls = gevt->GameBalls;
    ...
```

Repeat for `gameEvent_->Cars` (line 711).

`SyncLocalPlayers` (line 577) does:
```cpp
    if (!gameEvent_) {
        for (auto& slot : playerSlots_) {
            slot.assignedPC = nullptr;
        }
        return;
    }

    TArray<APlayerController_TA*> localPlayers = gameEvent_->LocalPlayers;
```

Replace with:
```cpp
    AGameEvent_Soccar_TA* gevt = gameEvent_;  // already under guiMutex_
    if (!gevt) {
        for (auto& slot : playerSlots_) {
            slot.assignedPC = nullptr;
        }
        return;
    }

    TArray<APlayerController_TA*> localPlayers = gevt->LocalPlayers;
```

### Step 4 — Make `OnGameEventDestroyed` take the lock when clearing `gameEvent_`

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find `OnGameEventDestroyed` (line 545):

```cpp
void BotModule::OnGameEventDestroyed(PreEvent& event) {
    (void)event;
    gameEvent_ = nullptr;
    botActive_.store(false);
    ...
```

Replace the first three lines of the function body with:

```cpp
void BotModule::OnGameEventDestroyed(PreEvent& event) {
    (void)event;
    {
        std::lock_guard<std::mutex> lock(guiMutex_);
        gameEvent_ = nullptr;
    }
    botActive_.store(false);
    ...
```

This ensures the snapshot in Step 1 sees either the live pointer or `nullptr`, never a half-published value.

### Step 5 — Same treatment for `OnGameEventStart`

```cpp
void BotModule::OnGameEventStart(PreEvent& event) {
    try {
        if (event.Caller() && event.Caller()->IsA(AGameEvent_Soccar_TA::StaticClass())) {
            std::lock_guard<std::mutex> lock(guiMutex_);
            gameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
            Console.Write("[GoySDK] Game event captured.");
        }
    } catch (const std::exception& e) {
        Console.Error(std::string("[GoySDK] Exception in OnGameEventStart: ") + e.what());
    } catch (...) {
        Console.Error("[GoySDK] Unknown exception in OnGameEventStart");
    }
}
```

Add the `std::lock_guard` line shown above.

## Verification

1. **Build** — `cmake --build`.
2. **Stress test** — enable both `autoSkipReplay_` and `autoForfeit_`, play 5 quick exhibition matches in a row, force-quitting the match in different states (mid-replay, mid-forfeit-vote-window, etc.). Pre-fix: occasional crash. Post-fix: no crashes.
3. **Read all the changes once more** — there should be **zero** occurrences of `gameEvent_->` or `gameEvent_.` inside `PlayerTickCalled`, `ReadGameState`, and `SyncLocalPlayers`. Run:
   ```bash
   awk '/^void BotModule::(PlayerTickCalled|ReadGameState|SyncLocalPlayers)/,/^}/' /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp | grep -n 'gameEvent_'
   ```
   Expected output: only the snapshot lines (`gevt = gameEvent_;` or `if (!gameEvent_)`) — no `gameEvent_->`.

## Don't do

- Do not just add another `if (!gameEvent_) return;` next to each dereference. There's still a window between the check and the use where the destroy handler can fire.
- Do not put the entire `PlayerTickCalled` body inside `guiMutex_`. The mutex is shared with `LockGuiState()` for GUI reads; holding it for the full inference path would serialize GUI updates against ticks and cause stutter.
- Do not catch SEH around the dereferences as a "fix." That's the existing `IsGameEventValid()` style — masks the bug, doesn't prevent it.
- Do not assume the hook framework guarantees same-thread dispatch. Even if it does today, the assumption shouldn't be load-bearing.

## Related
- **P1/07** — same shape of issue in `OverlayRenderer::IsInGame`/`CurrentGameEvent`. Apply the same snapshot-under-lock pattern there.
