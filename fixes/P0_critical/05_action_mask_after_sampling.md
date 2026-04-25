# P0 / 05 — Action mask defeats stochastic policy sampling

## TL;DR
`Bot::forward()` samples or argmaxes from unmasked logits. `BotModule::RunSlotInferenceTick` then applies the mask after the fact by replacing invalid sampled actions with deterministic masked argmax. That changes the action distribution from “softmax over allowed actions” into “unmasked sample, then deterministic repair,” which is not the policy distribution the model was trained against.

The mask must be applied to logits before argmax or sampling. The clean fix is to add a mask-aware `RLInference::Bot::forward(mask)` overload, share the actual forward implementation to avoid copy/paste drift, and make `BotModule` call the new overload.

## Where
- Post-hoc mask repair: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`, `RunSlotInferenceTick`, current lines around 891-916.
- Unmasked sampling: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`, `Bot::forward`, current lines around 246-291.
- Action masks and table: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/ActionMask.hpp`.
- Public inference API: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`.

## Why the Current Behavior Is Wrong
Training-side action masking normally does:

1. Compute logits.
2. Set invalid-action logits to `-inf`.
3. Sample or argmax from the masked logits.

Current deployment does:

1. Compute logits.
2. Sample or argmax from unmasked logits.
3. If the chosen action is invalid, replace it with deterministic masked argmax.

That is not equivalent. If several invalid actions have high probability, they all collapse to the same deterministic fallback action. This especially distorts low-boost, no-flip, and ground/air boundary states.

## Correct Fix Strategy
`BotModule` owns game-specific state and should build the mask from `PlayerSnapshot`. `RLInference` owns logits and should apply the mask before action selection. Keep that boundary:

- `GoySDK::BuildDefaultActionMask(slot.localSnapshot)` builds `std::vector<uint8_t>`.
- `RLInference::Bot::forward(mask)` applies it to logits.
- `BotModule` removes the post-hoc `SelectMaskedDiscreteActionIndex` replacement.

Do not move `PlayerSnapshot` or `ActionMask.hpp` into `RLInference`.

## Step 1 — Extend the RLInference API
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`.

Find:

```cpp
bool forward();
```

Replace with:

```cpp
bool forward();

/// Runs inference after applying a per-call discrete action mask.
///
/// `actionMask.size()` must equal the policy output dimension. Entries with
/// value 0 are forbidden and get their logits set to -infinity before argmax
/// or sampling. Returns false on shape mismatch or fully-masked input.
bool forward(const std::vector<uint8_t>& actionMask);
```

Then add this private declaration near the private fields:

```cpp
bool forward_impl(const std::vector<uint8_t>* actionMask);
```

No new include is needed for `std::vector`; the header already includes it.

## Step 2 — Add Required Includes
Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`.

Add:

```cpp
#include <limits>
```

near the existing standard-library includes.

## Step 3 — Refactor `Bot::forward` Into a Shared Implementation
Replace the body of `Bot::forward()` with:

```cpp
bool Bot::forward() {
    return forward_impl(nullptr);
}

bool Bot::forward(const std::vector<uint8_t>& actionMask) {
    return forward_impl(&actionMask);
}
```

Then move the old inference body into `Bot::forward_impl`.

Use this shape:

```cpp
bool Bot::forward_impl(const std::vector<uint8_t>* actionMask) {
    if (!impl_ || obs_.empty()) return false;

    if (!impl_->ready) {
        last_output_ = {};
        last_debug_.available = false;
        last_debug_.action_index = -1;
        last_debug_.logits.clear();
        return true;
    }

    try {
        torch::NoGradGuard ng;
        auto inp = torch::from_blob(
            obs_.data(), {1, static_cast<int64_t>(obs_.size())}, torch::kFloat32).clone();

        auto h = impl_->RunMLP(inp, impl_->shared);
        h = impl_->useLeaky ? torch::leaky_relu(h, 0.01) : torch::relu(h);
        auto logits = impl_->RunMLP(h, impl_->policy);

        auto flat = logits.squeeze().contiguous();
        const int nAct = static_cast<int>(flat.numel());

        if (actionMask && static_cast<int>(actionMask->size()) != nAct) {
            last_output_ = {};
            last_debug_.available = false;
            last_debug_.action_index = -1;
            last_debug_.logits.clear();
            return false;
        }

        if (actionMask) {
            auto* logitsData = flat.data_ptr<float>();
            bool anyAllowed = false;
            for (int i = 0; i < nAct; ++i) {
                if ((*actionMask)[static_cast<size_t>(i)] == 0) {
                    logitsData[i] = -std::numeric_limits<float>::infinity();
                } else {
                    anyAllowed = true;
                }
            }

            if (!anyAllowed) {
                last_output_ = {};
                last_debug_.available = false;
                last_debug_.action_index = -1;
                last_debug_.logits.clear();
                return false;
            }
        }

        last_debug_.available = true;
        last_debug_.logits.resize(nAct);
        std::memcpy(last_debug_.logits.data(), flat.data_ptr<float>(),
                    static_cast<size_t>(nAct) * sizeof(float));

        const int ai = config_.deterministic
            ? static_cast<int>(flat.argmax().item<int64_t>())
            : impl_->Sample(flat);
        last_debug_.action_index = ai;

        const auto& acts = GetDiscreteActions(); // replaced by config_.discrete_actions in P0/06
        last_output_ = (ai >= 0 && ai < static_cast<int>(acts.size())) ? acts[ai] : ActionOutput{};
        return true;
    } catch (...) {
        last_output_ = {};
        last_debug_.available = false;
        last_debug_.action_index = -1;
        last_debug_.logits.clear();
        return true;
    }
}
```

Important behavior:
- Mask-size mismatch returns `false`. Do **not** fall back to unmasked inference, because that silently reintroduces the bug.
- Fully masked input returns `false`. A fully masked state is a mask bug and should be visible.
- `last_debug_.logits` stores the masked logits. That makes diagnostic logs line up with the chosen action.

## Step 4 — Apply P0/06 on Top
If **P0/06** is applied in the same pass, replace:

```cpp
const auto& acts = GetDiscreteActions();
```

with:

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

This ensures model output dimension, mask dimension, and action-table dimension all agree before action selection.

## Step 5 — Update `BotModule::RunSlotInferenceTick`
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`.

Find the block where observations are pushed and `forward()` is called:

```cpp
slot.bot->obs().clear();
for (float val : obs) {
    slot.bot->push_obs(val);
}

if (!slot.bot->forward()) {
    if (diag) Console.Write("[GoySDK] S" + std::to_string(slotIdx) + " forward() false");
    return;
}
```

Replace with:

```cpp
slot.bot->obs().clear();
for (float val : obs) {
    slot.bot->push_obs(val);
}

const std::vector<uint8_t> actionMask = BuildDefaultActionMask(slot.localSnapshot);
if (!slot.bot->forward(actionMask)) {
    if (diag) {
        Console.Error("[GoySDK] S" + std::to_string(slotIdx) +
                      " forward(mask) false mask=" + std::to_string(actionMask.size()));
    }
    slot.lastAction.fill(0.f);
    ApplySlotInput(slotIdx, pc, 0.f, 0.f, 0.f, 0.f, 0.f, false, false, false);
    return;
}
```

The neutral output on failure prevents a stale previous action from being held when the new mask path detects a real mismatch.

## Step 6 — Remove Post-Hoc Repair
In the same function, delete this block:

```cpp
const RLInference::ActionOutput policyAction = action;
...
if (policyDebug.available) {
    const int maskedActionIndex = SelectMaskedDiscreteActionIndex(policyDebug.logits, slot.localSnapshot);
    if (maskedActionIndex >= 0 && maskedActionIndex != policyDebug.action_index) {
        action = ToActionOutput(maskedActionIndex);
    }
}
```

Keep the later defensive clamps:

```cpp
if (slot.localSnapshot.boost <= 0.01f) {
    action.boost = false;
}

if (!slot.localSnapshot.hasFlipOrJump && !slot.localSnapshot.isOnGround) {
    action.jump = false;
}
```

Those are safety clamps at the controller boundary. They should almost never fire after proper masking, but they are cheap and protect against model/action-table drift.

## Step 7 — Keep or Remove `SelectMaskedDiscreteActionIndex`
`SelectMaskedDiscreteActionIndex` can remain in `ActionMask.hpp` as a deterministic helper for tests or diagnostics. It should no longer be used in the live inference path.

After editing, run:

```bash
rg -n "SelectMaskedDiscreteActionIndex|policyAction" /Users/carterbarker/Documents/GoySDK/internal_bot
```

Expected:
- No `policyAction`.
- `SelectMaskedDiscreteActionIndex` only in `ActionMask.hpp`, unless a test uses it.

## Verification
1. Build `RLInference` and `GoySDKCore`.

2. Mask-size failure:
   - Temporarily pass `std::vector<uint8_t>{}` to `forward`.
   - Confirm the call returns false, logs `forward(mask) false`, applies neutral input, and does not keep the previous action.

3. Fully masked failure:
   - Temporarily pass a correctly-sized all-zero mask.
   - Confirm the same failure path.

4. Low boost stochastic test:
   - Enable `deterministic = false`.
   - Force `slot.localSnapshot.boost = 0`.
   - Confirm action indices vary across ticks while `action.boost` remains false.

5. Distribution regression:
   - For a fixed observation and fixed mask, sample 1,000 actions.
   - Confirm no masked action is selected.
   - Confirm the remaining distribution is not a single deterministic argmax unless the logits make it so.

6. Deterministic regression:
   - Enable `deterministic = true`.
   - Confirm action selection is masked argmax.

## Don't Do
- Do not implement post-hoc rejection sampling. It is not the same distribution as softmax over masked logits.
- Do not fall back to unmasked inference on mask mismatch.
- Do not duplicate the entire forward pass in two functions; share one implementation.
- Do not move `BuildDefaultActionMask` into `RLInference`. It depends on GoySDK-specific game state.

## Related
- **P0/06** — action-table ownership and dimension checks should be fixed in the same pass.
- **P2/01** — humanizer runs after action selection and should be retested once masked sampling is correct.
