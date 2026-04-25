# P1 / 05 — `autoRequeue_` and `autoChat_` are dead fields, not current UI features

## TL;DR
`BotModule::autoRequeue_` and `BotModule::autoChat_` exist in `BotModule.hpp` and `BotModule.cpp`, but the current GUI does **not** expose them and no implementation reads them. The earlier note described them as UI placebos, but that is inaccurate for the current tree. They are simply dead fields/accessors left over from an earlier automation plan.

The correct fix is to remove the fields and accessors unless the product owner explicitly wants to implement real auto-requeue/auto-chat features.

## Where
- Declarations: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`, current lines around 153-154.
- Accessors: same file, current lines around 164-165.
- Definitions: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, current lines around 80-81.
- Current GUI automation page: `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp`, around lines 1148-1227.

## Current Evidence
Run:

```bash
rg -n "AutoRequeue|AutoChat|autoRequeue_|autoChat_" /Users/carterbarker/Documents/GoySDK/internal_bot
```

Expected current matches:

```text
internal_bot/GoySDK/BotModule.hpp: static bool autoRequeue_;
internal_bot/GoySDK/BotModule.hpp: static bool autoChat_;
internal_bot/GoySDK/BotModule.hpp: static bool& AutoRequeue()
internal_bot/GoySDK/BotModule.hpp: static bool& AutoChat()
internal_bot/GoySDK/BotModule.cpp: bool BotModule::autoRequeue_ = false;
internal_bot/GoySDK/BotModule.cpp: bool BotModule::autoChat_ = false;
```

There should be no GUI matches and no behavior matches.

## Correct Fix Strategy
Remove the dead fields and accessors. Do not implement placeholder UI and do not add pseudocode behavior. Real auto-requeue and auto-chat require game-version-specific UFunction work and should be scoped separately.

## Step 1 — Delete Field Definitions
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Delete:

```cpp
bool                          BotModule::autoRequeue_ = false;
bool                          BotModule::autoChat_ = false;
```

## Step 2 — Delete Field Declarations
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.hpp`.

Delete:

```cpp
static bool autoRequeue_;
static bool autoChat_;
```

## Step 3 — Delete Public Accessors
In the same header, delete:

```cpp
static bool& AutoRequeue()          { return autoRequeue_; }
static bool& AutoChat()             { return autoChat_; }
```

## Step 4 — Verify GUI Does Not Need Changes
The current GUI automation page exposes:

- `AutoSkipReplay`
- `AutoForfeit`
- `AutoForfeitScoreDiff`
- `AutoForfeitTimeSec`

It does not call `AutoRequeue()` or `AutoChat()`. Therefore no GUI deletion is required unless a future branch has those toggles.

Run:

```bash
rg -n "AutoRequeue|AutoChat" /Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/GUI.cpp
```

Expected: no matches.

## Step 5 — If You Actually Want These Features Later
Do not re-add the booleans without behavior. A real implementation needs a separate design:

### Auto Requeue
Required details before implementation:
- Which exact Rocket League UFunction starts matchmaking for the current playlist?
- Where is the last playlist/mode stored?
- Should this operate only in private matches, casual, ranked, or never in online queues?
- What cooldown prevents repeated queue calls?
- What UI state confirms the post-match screen is ready?

### Auto Chat
Required details before implementation:
- Which chat/quickchat UFunction is safe to call?
- Which events trigger messages?
- What cooldown/rate limit prevents spam?
- Should it be disabled for online play?

Until those details are answered, dead fields are worse than no feature because they suggest automation exists when it does not.

## Verification
1. Build `GoySDKCore`.

2. Static search:
   ```bash
   rg -n "AutoRequeue|AutoChat|autoRequeue_|autoChat_" /Users/carterbarker/Documents/GoySDK/internal_bot
   ```
   Expected: no matches.

3. GUI smoke:
   - Open the Automation page.
   - Confirm Skip Replays and Auto Forfeit still render.
   - Confirm no layout depends on the removed accessors.

## Don't Do
- Do not leave the accessors returning a dummy static local.
- Do not add “coming soon” checkboxes.
- Do not implement requeue by synthetic keyboard input; if the feature is ever added, use validated in-engine calls.
- Do not add chat automation without cooldown/rate limiting and explicit scope.

## Related
- **P1/04** — join countdown is different: it has a real state machine and only needs a trigger.
