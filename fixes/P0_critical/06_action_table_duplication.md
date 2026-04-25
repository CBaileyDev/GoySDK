# P0 / 06 — Action tables duplicated in two libraries with no runtime contract

## TL;DR
The discrete action table is built independently in two places:

- `repos/RLInference/src/Bot.cpp`, `GetDiscreteActions()`, used to decode policy output index into controller commands.
- `internal_bot/GoySDK/ActionMask.hpp`, `GetDefaultDiscreteActionTables()`, used to build masks and to map masked indices back to actions.

The two tables currently appear to match, but there is no runtime or compile-time contract. If either table changes, policy output index `i` can decode to different controller actions in inference vs. masking. That silently corrupts bot behavior.

Fix this by making GoySDK the single owner of the action table and injecting that table into `RLInference::BotConfig`. Add mandatory dimension checks so a missing or mismatched table is a hard inference failure, not a neutral-output mystery.

## Where
- RLInference copy: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`, `GetDiscreteActions`, current lines around 101-125.
- GoySDK source of truth: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/ActionMask.hpp`, `GetDefaultDiscreteActionTables`, current lines around 35-115.
- Bot config: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`, `BotConfig`.
- Bot construction: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, `LoadBotForSlot`, current lines around 264-286.

## Correct Fix Strategy
Use `ActionMask.hpp` as the single source of truth because it already owns:

- action enumeration,
- ground/air masks,
- jump/boost masks,
- game-state-specific masking rules,
- conversion to `RLInference::ActionOutput`.

`RLInference` should not independently know how GoySDK enumerates actions. It should receive the action table from its host and validate that the policy output dimension matches it.

## Step 1 — Add `discrete_actions` to `BotConfig`
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`.

Find `struct BotConfig`.

Add this field after `bool use_leaky_relu = false;`:

```cpp
/// Host-provided discrete action table. `discrete_actions[i]` is the controller
/// output corresponding to policy logit index `i`. This must match the policy's
/// training-time action ordering and the final policy output dimension.
std::vector<ActionOutput> discrete_actions{};
```

No new include is needed; the header already includes `<vector>` and `ActionOutput.hpp`.

## Step 2 — Delete RLInference's Local Action Table
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`.

Delete the whole `GetDiscreteActions()` function:

```cpp
// Discrete action table (mirrors GoySDK::ActionMask.hpp exactly).
static const std::vector<ActionOutput>& GetDiscreteActions() {
    ...
}
```

After this deletion, `rg -n "GetDiscreteActions" repos/RLInference internal_bot` should not find the RLInference helper.

## Step 3 — Decode Actions From `config_.discrete_actions`
In `Bot::forward_impl` from **P0/05**, after logits are available and `nAct` is known, add a mandatory action-table check before choosing or decoding an action:

```cpp
const auto& acts = config_.discrete_actions;
if (static_cast<int>(acts.size()) != nAct) {
    last_output_ = {};
    last_debug_.available = false;
    last_debug_.action_index = -1;
    last_debug_.logits.clear();
    return false;
}
```

Then decode using the injected table:

```cpp
last_output_ = (ai >= 0 && ai < static_cast<int>(acts.size()))
    ? acts[static_cast<size_t>(ai)]
    : ActionOutput{};
```

If **P0/05** is not applied yet, add this same check to the existing `Bot::forward()` after `nAct` is computed and before action selection. Do not leave action-table-size validation as optional.

## Step 4 — Inject the Table From `BotModule::LoadBotForSlot`
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Inside `LoadBotForSlot`, after constructing `botCfg` and before setting model data is fine. The current construction area is around:

```cpp
RLInference::BotConfig botCfg(
    RLInference::BotType::GigaLearn,
    "",
    "",
    slot.config.sharedHeadSizes,
    slot.config.tickSkip,
    false,
    slot.config.GetExpectedObsCount(),
    slot.config.policySizes,
    slot.config.useLeakyRelu
);
botCfg.primary_model_data = sharedData;
```

Insert:

```cpp
{
    const auto& src = GetDefaultDiscreteActions();
    botCfg.discrete_actions.reserve(src.size());
    for (const auto& action : src) {
        botCfg.discrete_actions.push_back(ToActionOutput(action));
    }
}
```

`BotModule.cpp` already includes `ActionMask.hpp`, so `GetDefaultDiscreteActions` and `ToActionOutput(const DiscreteAction&)` are available.

## Step 5 — Validate Mask and Action Table Dimensions Together
When **P0/05** is also applied, `Bot::forward_impl` should validate:

1. `config_.discrete_actions.size() == nAct`.
2. If a mask was provided, `actionMask.size() == nAct`.
3. The mask has at least one allowed action.

The validation order should be:

```cpp
const int nAct = static_cast<int>(flat.numel());

const auto& acts = config_.discrete_actions;
if (static_cast<int>(acts.size()) != nAct) {
    // missing host action table or model/table mismatch
    clear debug/output;
    return false;
}

if (actionMask && static_cast<int>(actionMask->size()) != nAct) {
    // model/table/mask mismatch
    clear debug/output;
    return false;
}

if (actionMask) {
    // apply mask and reject all-zero mask
}
```

Do not apply the mask before proving the action table matches the policy dimension. A model/action-table mismatch is a configuration bug, not a mask bug.

## Step 6 — Add Host-Side Sanity Logging
In `LoadBotForSlot`, after table injection and before constructing `RLInference::Bot`, add a debug-only or early-load diagnostic if needed:

```cpp
if (botCfg.discrete_actions.empty()) {
    Console.Error("[GoySDK] Discrete action table is empty; refusing to load bot.");
    slot.modelIdx = -1;
    modelLoading_ = false;
    return false;
}
```

Do not only log and continue. An empty table means every action index would decode to neutral.

Optional one-time diagnostic:

```cpp
Console.Write("[GoySDK] Discrete action table size: " + std::to_string(botCfg.discrete_actions.size()));
```

If added, remove or gate it after verification so it does not spam every model reload.

## Step 7 — Remove Dead Conversion Use If P0/05 Removes Post-Hoc Masking
After **P0/05**, the live path should no longer call:

```cpp
ToActionOutput(maskedActionIndex)
```

The `ToActionOutput(int)` overload in `ActionMask.hpp` can remain for diagnostics/tests. The important part is that live inference decodes through `config_.discrete_actions`.

## Verification
1. Build `RLInference`.
2. Build `GoySDKCore`.

3. Search:
   ```bash
   rg -n "GetDiscreteActions" /Users/carterbarker/Documents/GoySDK/repos /Users/carterbarker/Documents/GoySDK/internal_bot
   ```
   Expected: no RLInference-local `GetDiscreteActions` helper.

4. Runtime table-size sanity:
   - Temporarily log `botCfg.discrete_actions.size()`.
   - Confirm it matches the policy output dimension printed in early inference debug (`policyDebug.logits.size()`).

5. Empty-table failure:
   - Temporarily comment out the injection block.
   - Confirm `Bot::forward` returns false and the host applies neutral input, instead of silently emitting neutral from an out-of-range action index.

6. Drift simulation:
   - Temporarily add or remove one action from `GetDefaultDiscreteActionTables()`.
   - Confirm model output/action-table mismatch is caught by `forward_impl` and logged through the failure path.

7. Behavior regression:
   - With the table unchanged, record `policyDebug.action_index` and controller output before and after the refactor for the same observation.
   - They should match exactly.

## Don't Do
- Do not keep both action tables and add only a size check. Reordering bugs can keep the same size and still corrupt behavior.
- Do not move `ActionMask.hpp` into `RLInference`; it depends on GoySDK-specific state.
- Do not load the action table from JSON at runtime. The static C++ table is fine; it just needs one owner.
- Do not allow missing `discrete_actions` to fall back to an internal default. That reintroduces two sources of truth.

## Related
- **P0/05** — mask-before-sampling should be applied in the same implementation pass.
