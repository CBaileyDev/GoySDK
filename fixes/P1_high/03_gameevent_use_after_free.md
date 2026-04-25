# P1 / 03 — `gameEvent_` is dereferenced late in `PlayerTickCalled` without a stable snapshot

## TL;DR
`BotModule::PlayerTickCalled` checks `IsGameEventValid()` at the top, but later blocks dereference `gameEvent_` again for replay skipping and auto-forfeit. The old proposed fix suggested copying `gameEvent_` into a raw local pointer under `guiMutex_`. That is not enough: a raw pointer snapshot protects only the value stored in our static field, not the lifetime of the underlying Unreal object.

The better fix is to stop dereferencing `gameEvent_` in late automation blocks. Capture the match fields those blocks need during the per-frame state read, store them in a plain data snapshot, and have automation consume the snapshot. Lock `gameEvent_` publication/clear so the pointer field is not raced, but do not treat locking as object-lifetime ownership.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Top guard: `PlayerTickCalled`, current line around 599.
- Late unguarded uses:
  - `gameEvent_->IsInReplayPlayback()` around line 629.
  - `gameEvent_->bCanVoteToForfeit` around line 642.
  - `gameEvent_->GameStateTimeRemaining` around line 645.
  - `gameEvent_->Teams` around line 646.
- Pointer write/clear:
  - `OnGameEventStart` around line 535.
  - `OnGameEventDestroyed` around line 547.

## Why the Old “Local Pointer Snapshot” Is Not Enough
This pattern:

```cpp
AGameEvent_Soccar_TA* gevt = gameEvent_;
```

prevents `gameEvent_` itself from changing under your feet, but it does not keep the Unreal object alive. If the engine destroys that object, `gevt` is still a dangling pointer. A mutex owned by GoySDK does not synchronize with Unreal's allocator or object lifetime.

Therefore, use local raw pointers only while reading engine state in one small, controlled block, then convert the result into plain values that can be safely used later in the tick.

## Correct Fix Strategy
1. Add a `MatchStateSnapshot` struct containing only plain data needed by automation.
2. Reset that snapshot on game-event destroy.
3. Fill it once per frame during the existing `ReadGameState` pass.
4. Replace late `gameEvent_->...` reads in `PlayerTickCalled` with snapshot reads.
5. Lock `gameEvent_` publication/clear so our pointer field and snapshot are internally consistent.
6. Keep `VoteToForfeit` as the only remaining engine call in the auto-forfeit block; it uses `pc->PRI->Team`, not `gameEvent_`.

This does not magically own Unreal object lifetime, but it shrinks the dangerous `gameEvent_` dereference surface to the centralized game-state read.

## Step 1 — Add a Plain Match Snapshot
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`.

Inside namespace `GoySDK`, near `PlayerBotSlot`, add:

```cpp
struct MatchStateSnapshot {
    bool valid = false;
    bool inReplayPlayback = false;
    bool canVoteToForfeit = false;
    int gameStateTimeRemaining = 0;
    int teamScores[2] = {0, 0};
};
```

Then add this private static field near `ballSnapshot_` / `allPlayers_`:

```cpp
static MatchStateSnapshot matchState_;
```

## Step 2 — Define and Reset It
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Add the static definition near the other static fields:

```cpp
MatchStateSnapshot BotModule::matchState_{};
```

In `OnGameEventDestroyed`, reset it with the rest of match state:

```cpp
matchState_ = {};
```

Also reset it in `OnCreate()`:

```cpp
matchState_ = {};
```

## Step 3 — Lock `gameEvent_` Publication and Clear
In `OnGameEventStart`, replace:

```cpp
gameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
Console.Write("[GoySDK] Game event captured.");
```

with:

```cpp
{
    std::lock_guard<std::mutex> lock(guiMutex_);
    gameEvent_ = static_cast<AGameEvent_Soccar_TA*>(event.Caller());
    matchState_ = {};
}
Console.Write("[GoySDK] Game event captured.");
```

In `OnGameEventDestroyed`, replace the direct clear:

```cpp
gameEvent_ = nullptr;
```

with:

```cpp
{
    std::lock_guard<std::mutex> lock(guiMutex_);
    gameEvent_ = nullptr;
    matchState_ = {};
}
```

Do not put the entire destroy handler under `guiMutex_` unless you audit for lock ordering. Only protect the shared pointer/snapshot fields.

## Step 4 — Add a Non-Throwing Snapshot Builder
Add this private helper in `BotModule.cpp`, near `ReadGameState`:

```cpp
static GoySDK::MatchStateSnapshot BuildMatchStateSnapshot(AGameEvent_Soccar_TA* gevt) {
    GoySDK::MatchStateSnapshot snapshot{};
    if (!gevt) {
        return snapshot;
    }

    __try {
        snapshot.valid = true;
        snapshot.inReplayPlayback = gevt->IsInReplayPlayback();
        snapshot.canVoteToForfeit = gevt->bCanVoteToForfeit != 0;
        snapshot.gameStateTimeRemaining = gevt->GameStateTimeRemaining;

        auto teams = gevt->Teams;
        if (teams.size() >= 2) {
            snapshot.teamScores[0] = teams[0] ? teams[0]->Score : 0;
            snapshot.teamScores[1] = teams[1] ? teams[1]->Score : 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = {};
    }

    return snapshot;
}
```

Rationale:
- This keeps dangerous Unreal reads in one place.
- If a read faults, the snapshot becomes invalid and automation does nothing.
- `PlayerTickCalled` no longer needs to touch `gameEvent_` for replay/score state.

## Step 5 — Use a Local `gevt` Only Inside `ReadGameState`
Edit `BotModule::ReadGameState`.

Current start:

```cpp
void BotModule::ReadGameState(APlayerController_TA* anyPC) {
    (void)anyPC;
    if (!IsGameEventValid()) return;
    allPlayers_.clear();

    TArray<ABall_TA*> balls = gameEvent_->GameBalls;
```

Replace with:

```cpp
void BotModule::ReadGameState(APlayerController_TA* anyPC) {
    (void)anyPC;
    if (!IsGameEventValid()) {
        matchState_ = {};
        return;
    }

    AGameEvent_Soccar_TA* gevt = gameEvent_;
    if (!gevt) {
        matchState_ = {};
        return;
    }

    matchState_ = BuildMatchStateSnapshot(gevt);
    allPlayers_.clear();

    TArray<ABall_TA*> balls = gevt->GameBalls;
```

Then replace the later `gameEvent_->Cars` with:

```cpp
TArray<ACar_TA*> cars = gevt->Cars;
```

This function is currently called inside the `guiMutex_` block from `PlayerTickCalled`, so do not add another `std::lock_guard<std::mutex> lock(guiMutex_)` inside it or you will deadlock with a non-recursive mutex.

## Step 6 — Use a Local `gevt` Only Inside `SyncLocalPlayers`
Edit `SyncLocalPlayers`.

Replace:

```cpp
if (!gameEvent_) {
    ...
}

TArray<APlayerController_TA*> localPlayers = gameEvent_->LocalPlayers;
```

with:

```cpp
AGameEvent_Soccar_TA* gevt = gameEvent_;
if (!gevt) {
    for (auto& slot : playerSlots_) {
        slot.assignedPC = nullptr;
    }
    return;
}

TArray<APlayerController_TA*> localPlayers = gevt->LocalPlayers;
```

This is still a raw engine read, but it is centralized in the per-frame sync phase instead of scattered through automation code.

## Step 7 — Replace Auto-Skip Replay Dereference
In `PlayerTickCalled`, replace:

```cpp
if (gameEvent_->IsInReplayPlayback()) {
    pc->bOverrideInput = 1;
    pc->OverrideInput.bJump = 1;
    skipReplayCooldown_ = 30;
}
```

with:

```cpp
const MatchStateSnapshot matchState = matchState_;
if (matchState.valid && matchState.inReplayPlayback) {
    pc->bOverrideInput = 1;
    pc->OverrideInput.bJump = 1;
    skipReplayCooldown_ = 30;
}
```

Because `PlayerTickCalled` already refreshes `matchState_` in the first-slot block, this reads the most recent per-frame data without touching `gameEvent_`.

## Step 8 — Replace Auto-Forfeit `gameEvent_` Reads
Replace the current condition:

```cpp
if (autoForfeit_ && !forfeitVotedThisMatch_ && gameEvent_->bCanVoteToForfeit) {
```

with:

```cpp
const MatchStateSnapshot matchState = matchState_;
if (autoForfeit_ && !forfeitVotedThisMatch_ &&
    matchState.valid && matchState.canVoteToForfeit) {
```

Inside the block, replace:

```cpp
int timeRemaining = gameEvent_->GameStateTimeRemaining;
auto teams = gameEvent_->Teams;
if (teams.size() >= 2 && localPRI && localPRI->Team) {
    int myTeamIdx = localPRI->Team->TeamIndex;
    int myScore = teams[myTeamIdx]->Score;
    int oppScore = teams[1 - myTeamIdx]->Score;
```

with:

```cpp
int timeRemaining = matchState.gameStateTimeRemaining;
if (localPRI && localPRI->Team) {
    int myTeamIdx = localPRI->Team->TeamIndex;
    if (myTeamIdx >= 0 && myTeamIdx <= 1) {
        int myScore = matchState.teamScores[myTeamIdx];
        int oppScore = matchState.teamScores[1 - myTeamIdx];
        ...
    }
```

Keep:

```cpp
auto* myTeam = static_cast<ATeam_TA*>(localPRI->Team);
if (myTeam) {
    myTeam->VoteToForfeit(localPRI);
    ...
}
```

That call still touches engine objects, but it is the actual action the feature needs to perform. The important cleanup is that it no longer walks through `gameEvent_` after the top-of-tick phase.

## Step 9 — Static Check
After editing, run:

```bash
awk '/void BotModule::PlayerTickCalled/,/^}/' /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp | rg "gameEvent_->|gameEvent_\\."
```

Expected: no matches inside `PlayerTickCalled`.

Also run:

```bash
awk '/void BotModule::ReadGameState/,/^}/' /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp | rg "gameEvent_->"
```

Expected: no direct `gameEvent_->`; use `gevt->` only.

## Verification
1. Build `GoySDKCore`.

2. Replay-skip test:
   - Enable auto-skip replay.
   - Score several goals and confirm jump is sent during replay.
   - Confirm no direct `gameEvent_` use remains in the replay block.

3. Auto-forfeit test:
   - Enable auto-forfeit with a low threshold.
   - Enter a match state where forfeit is allowed.
   - Confirm scores and time come from `matchState_` and the vote still fires.

4. Transition stress:
   - Enable auto-skip and auto-forfeit.
   - Enter/leave matches repeatedly, including during replay and forfeit windows.
   - Confirm no crash and no stale automation after `OnGameEventDestroyed`.

5. Snapshot invalid test:
   - Temporarily force `matchState_.valid = false`.
   - Confirm auto-skip and auto-forfeit do nothing.

## Don't Do
- Do not treat `std::lock_guard` around a raw pointer copy as object-lifetime ownership.
- Do not put the entire inference path under `guiMutex_`; it will create avoidable contention and possible stutter.
- Do not keep late `gameEvent_->...` reads in `PlayerTickCalled`.
- Do not swallow structured exceptions and continue with partially-filled match state. If the snapshot read faults, mark the snapshot invalid.

## Related
- **P1/07** — overlay has the same raw-pointer/state-publication problem.
