# P1 / 05 — `autoRequeue_` and `autoChat_` are pure UI placebos

## TL;DR
`BotModule::autoRequeue_` and `BotModule::autoChat_` are static booleans with public getter/setter accessors used by the GUI. The user can toggle them in the menu. They are **never read** in any implementation file — flipping them does literally nothing.

## Where
- Field declarations: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp` lines **153–154**
- Field definitions: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp` lines **80–81**
- Public mutator accessors: `BotModule.hpp` lines **164–165** (`AutoRequeue()`, `AutoChat()`)
- GUI consumers (toggle the booleans): `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp` (search `BotMod.AutoRequeue` / `BotMod.AutoChat`)
- Implementation consumers: **none** (verified via `grep -rn "autoRequeue_\|autoChat_" /Users/carterbarker/Documents/GoySDK/internal_bot/`)

## Problem
The GUI exposes "Auto Requeue" and "Auto Chat" checkboxes. Users assume they work. They do not. The boolean state lives in memory and decays at DLL unload without ever influencing behavior.

## Why it matters
Either implement them or remove them. Shipping a checkbox that does nothing is a UX trap — when something looks broken (e.g., requeue not happening), the user blames a real bug they think is unrelated.

## Root cause
Author scaffolded the toggles before implementing the underlying behavior, then never came back. Common pattern.

## Fix

Pick **Option A (implement)** or **Option B (remove)**. Removal is the safer default; only choose implementation if the engineering cost is justified.

### Option A — Implement the features

#### `autoRequeue_`
After a match ends (post-game screen visible), invoke the same UFunction the user-facing "Play" button calls. Search for it in `Functions.hpp`:
```bash
grep -n "fn_play\|Play_TA\|RequestQueue" /Users/carterbarker/Documents/GoySDK/internal_bot/Functions.hpp | head
```

Hook the post-match event (`fn_event_destroyed` is already hooked). In `BotModule::OnGameEventDestroyed` (line 545), after the cleanup block, add:
```cpp
    if (autoRequeue_) {
        // The exact UFunction varies by game version. Common candidates:
        //   AGFxData_PlayerTitles_TA::StartMatchmaking()
        //   AOnlineGameInterface::RequestPlaylist()
        // Use Console.Write to confirm the function exists in the dump first.
        if (auto* gvc = Instances.GetInstanceOf<UGameViewportClient_TA>()) {
            // Pseudocode — fill in the exact call once you've identified the right UFunction.
            // gvc->StartMatchmaking(<lastPlaylistId>);
            Console.Write("[GoySDK] Auto-requeue triggered.");
        }
    }
```

Mark with `// TODO(autorequeue): bind to actual matchmaking UFunction` so future readers know this needs validation.

#### `autoChat_`
Send a quickchat after key events (goal scored, save, etc.). Hook `Function TAGame.GFxHUD_TA.HandleStatTickerMessage` or similar. The implementation pattern:
```cpp
    if (autoChat_) {
        if (auto* hud = Instances.GetInstanceOf<AGFxHUD_TA>()) {
            // hud->SendChatMessage(...) — exact signature depends on your SDK dump.
        }
    }
```

This is more work than autoRequeue and Riot/Psyonix may consider it spammy. Defer if uncertain.

### Option B — Remove the toggles (recommended unless someone owns the implementation)

#### Step B1 — Remove the field definitions
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find lines 80–81:
```cpp
bool                          BotModule::autoRequeue_ = false;
bool                          BotModule::autoChat_ = false;
```
Delete both lines.

#### Step B2 — Remove the field declarations
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`. Find lines 153–154:
```cpp
    static bool autoRequeue_;      
    static bool autoChat_;         
```
Delete both lines.

#### Step B3 — Remove the public accessors
In the same file, find lines 164–165:
```cpp
    static bool& AutoRequeue()          { return autoRequeue_; }
    static bool& AutoChat()             { return autoChat_; }
```
Delete both lines.

#### Step B4 — Remove the GUI checkboxes
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp`. Search for `AutoRequeue` and `AutoChat`:
```bash
grep -n "AutoRequeue\|AutoChat" /Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp
```
Delete the `ImGui::Checkbox` lines (and any associated label / tooltip / spacing) that reference them.

#### Step B5 — Verify clean build
```bash
grep -rn "autoRequeue_\|autoChat_\|AutoRequeue\|AutoChat" /Users/carterbarker/Documents/GoySDK/
```
Expected: zero hits in the GoySDK source. (The fixes/ folder will still contain mentions; ignore those.)

## Verification

### If you went with Option A
1. **Build**.
2. **Auto-requeue test** — enable, finish a match, confirm the "searching for match" UI appears within 5 seconds.
3. **Auto-chat test** — enable, score a goal, confirm a quickchat fires.

### If you went with Option B
1. **Build** — must succeed with zero unused-variable warnings.
2. **GUI sanity check** — inject the DLL, open the menu, confirm the two checkboxes are gone and no layout artifacts remain.

## Don't do

- Do not leave the toggles in the GUI with a "(coming soon)" label. That's just deferring the same problem.
- Do not silently rename the toggles — users reading old changelogs will hunt for them.
- Do not implement Option A by simulating keyboard input (`SendInput("X")`). The host game has anti-cheat heuristics that look for synthetic input; use the in-engine UFunction call.

## Related
- **P1/04** — `joinPressCountdown_` is the same family (declared, scaffolded, missing the trigger). Same audit pattern would have caught both.
