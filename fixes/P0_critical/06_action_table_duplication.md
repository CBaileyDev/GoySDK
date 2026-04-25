# P0 / 06 — Action tables duplicated in two libraries with no compile-time link

## TL;DR
The discrete action table (~64 actions) is **independently re-implemented** in two places:

- `repos/RLInference/src/Bot.cpp` lines 102–125 — used to decode the policy's `argmax`/`Sample` index into a controller output.
- `internal_bot/GoySDK/ActionMask.hpp` lines 35–115 — used to build masks and to map indices back to controller outputs in BotModule.

Both happen to match today. A one-line change in either file silently re-indexes every action and rotates the policy's output by one slot. There is no `static_assert`, no shared header, no test that asserts equivalence.

## Where
- File 1: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp` (function `GetDiscreteActions`, lines 102–125)
- File 2: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/ActionMask.hpp` (function `GetDefaultDiscreteActionTables`, lines 35–115)

## Problem
The two tables are written with the same loop nesting and the same skip conditions, so they currently produce identical sequences. But:

- They live in two repos with separate review cycles.
- The skip-rule logic (`if (boost == 1.0f && throttle != 1.0f) continue;` etc.) is non-trivial and easy to break.
- The struct types differ (`RLInference::ActionOutput` vs `GoySDK::DiscreteAction`) — even if both lists conceptually match, a future refactor on one side won't necessarily be mirrored on the other.

The user-visible signal of drift is "the bot's actions look weird and steer when it should boost." There's no fast way to diagnose that, because both files are individually correct.

## Why it matters
This is a **time bomb**. Today: works. Tomorrow: someone changes `kBinary` or adds an action variant in one place. The model still produces a logit vector of size N, the index decode still returns *some* `ActionOutput`, but the wrong one. There's no exception, no log, no visible error.

## Root cause
RLInference was extracted into a separate library to make the inference code testable in isolation. The action table got copied because the type system on the GoySDK side wanted `DiscreteAction` (with a structured mask), and the RLInference side wanted `ActionOutput` (the controller-facing shape). Nobody added a link between them.

## Fix

There are three valid resolutions; pick **option B** unless you have a reason not to.

### Option A — Single source of truth in RLInference
Move the action enumeration into `RLInference` and have GoySDK consume it. **Don't do this** because it puts game-specific action shapes in a library that's supposed to be game-agnostic.

### Option B — Single source of truth in GoySDK, RLInference receives the table at construction (recommended)
Make `RLInference::Bot` parameterized over the action table. The host injects it at construction time.

### Option C — Compile-time check that the two tables match
Keep both, add a `static_assert` that they're the same length, and a unit test that walks both and compares element-by-element.

#### Option B implementation

##### Step B1 — Add the action table to `BotConfig`

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`. Find the `BotConfig` struct (line 24). Add this field, right after `bool use_leaky_relu = false;`:

```cpp
    /// Discrete action table the policy was trained against. The element at
    /// index `i` is the controller output the policy intends to emit when its
    /// argmax/sample picks index `i`. Length must match the final policy
    /// output dimension.
    std::vector<ActionOutput> discrete_actions{};
```

##### Step B2 — Have `Bot::forward` use the injected table instead of `GetDiscreteActions`

In `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`, find `GetDiscreteActions` (lines 102–125) and **delete it entirely**.

Then find the line:
```cpp
        const auto& acts = GetDiscreteActions();
        last_output_ = (ai >= 0 && ai < (int)acts.size()) ? acts[ai] : ActionOutput{};
```
(in `Bot::forward` near line 282–283; also in the new `Bot::forward(mask)` from P0/05).

Replace with:
```cpp
        const auto& acts = config_.discrete_actions;
        last_output_ = (ai >= 0 && ai < (int)acts.size()) ? acts[ai] : ActionOutput{};
```

##### Step B3 — Have `BotModule::LoadBotForSlot` inject the table

In `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, find the `RLInference::BotConfig botCfg(...)` construction (lines 265–275). After the construction and before `botCfg.primary_model_data = sharedData;`, add:

```cpp
        // Inject the GoySDK action table — the policy was trained against this exact
        // ordering. RLInference no longer owns its own copy (P0/06).
        {
            const auto& src = GetDefaultDiscreteActions();
            botCfg.discrete_actions.reserve(src.size());
            for (const auto& a : src) {
                botCfg.discrete_actions.push_back(ToActionOutput(a));
            }
        }
```

`GetDefaultDiscreteActions` and `ToActionOutput(const DiscreteAction&)` are already defined in `ActionMask.hpp`, which `BotModule.cpp` already includes.

##### Step B4 — Add a runtime assertion in `Bot::forward`

In the existing `Bot::forward` (and the new `Bot::forward(mask)` from P0/05), at the start of the `try { torch::NoGradGuard ng; ... }` block, add:

```cpp
        if (config_.discrete_actions.empty()) {
            // Misconfigured caller — they didn't inject the action table.
            return false;
        }
```

This catches "I forgot to populate `discrete_actions`" with a hard failure rather than silent neutral output. (Optional: also assert `static_cast<int>(config_.discrete_actions.size()) == nAct` once `nAct` is known.)

##### Step B5 — Verify nothing else references the deleted `GetDiscreteActions`

```bash
grep -rn "GetDiscreteActions" /Users/carterbarker/Documents/GoySDK/repos/ /Users/carterbarker/Documents/GoySDK/internal_bot/
```
Should be zero hits. (`GetDefaultDiscreteActions` is the GoySDK side and is fine.)

## Verification

1. **Build** — both libraries.
2. **Sanity-check the table size at runtime** — temporarily add `Console.Write("[GoySDK] action table size: " + std::to_string(botCfg.discrete_actions.size()));` and confirm it matches the model's output dim.
3. **Behavioral regression** — record `policyDebug.action_index` and `last_output_` for 100 ticks before and after the change with the same observation. They should be identical (because the table contents haven't changed, only the *ownership* has).
4. **Drift simulation** — temporarily add a sentinel action `built.actions.push_back({...})` *only* in `ActionMask.hpp::GetDefaultDiscreteActionTables`. Confirm that the size mismatch is now caught (either by the assertion in B4 or by the existing logits-vs-mask size check at `BotModule.cpp:878`).

## Don't do

- Do not edit `ActionMask.hpp` to import from `RLInference.hpp` — that creates a circular dependency.
- Do not ship Option C alone (compile-time check). It catches *some* drift (size differences, type mismatches) but not the most likely class of bug (someone reorders a loop).
- Do not turn the action table into a runtime-loaded JSON. The current static array is correct; it just needs one owner.

## Related
- **P0/05** — same `Bot::forward` is being modified to take a mask. Do P0/05 first (the mask change is more invasive) and apply this on top.
