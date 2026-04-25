# P0 / 05 — Action mask defeats stochastic policy sampling

## TL;DR
`Bot::forward()` samples an action stochastically when `deterministic=false`. `BotModule::RunSlotInferenceTick` then masks the result by recomputing the **argmax over masked logits** and overwriting the sampled action. So whenever the sampled action is masked-out, the policy silently degrades to deterministic argmax — and even when it isn't, the user thinks they're getting stochastic behavior but they aren't, because the masking process post-hoc replaces invalid samples deterministically.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`
- Function: `BotModule::RunSlotInferenceTick`
- Lines: **911–916**

Existing code:
```cpp
if (policyDebug.available) {
    const int maskedActionIndex = SelectMaskedDiscreteActionIndex(policyDebug.logits, slot.localSnapshot);
    if (maskedActionIndex >= 0 && maskedActionIndex != policyDebug.action_index) {
        action = ToActionOutput(maskedActionIndex);
    }
}
```

And in `Bot.cpp:277-279`:
```cpp
int ai = config_.deterministic
    ? static_cast<int>(flat.argmax().item<int64_t>())
    : impl_->Sample(flat);
```

## Problem
The training environment (RLGym/etc.) sets logits of invalid actions to `-inf` *before* sampling. The deployment side does the opposite: it samples first, then "fixes" invalid samples by switching to a deterministic argmax over the mask. This produces a different action distribution than the policy was trained to produce.

Two concrete symptoms:
1. When a sampled action is invalid (e.g. boost when empty), it's replaced by `argmax(masked logits)`. Multiple distinct sampled invalid actions all collapse to the same deterministic substitute. **Mode collapse.**
2. The human-tunable `deterministic` flag becomes meaningless for any state where the policy puts non-trivial probability mass on masked actions.

## Why it matters
The bot's behavior at the boundary (low boost, off-ground, mid-jump) deviates from the trained policy without anyone noticing. Subjective "the bot got worse" reports become unfixable because the test-environment logs look fine — masking happens correctly there.

## Root cause
The masking infrastructure was added after the inference path was built, and the author bolted it on at the wrong layer. The right place is *inside* `Bot::forward`, before sampling.

## Fix

This fix changes the contract: `Bot::forward` becomes mask-aware. The mask must be computed by the caller (BotModule has the `PlayerSnapshot`) and passed into `forward`.

### Step 1 — Extend the `Bot` API to accept a per-call mask

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/include/RLInference.hpp`. Find:
```cpp
    bool forward();
```
Replace with:
```cpp
    bool forward();

    /// Forward pass with a per-call action mask. `mask.size()` must equal the
    /// number of discrete actions; entries with `mask[i] == 0` are forbidden
    /// and have their logits set to -inf before softmax/sampling.
    /// Returns false if `mask.size()` doesn't match the policy output dim, or
    /// if the bot is in stub mode.
    bool forward(const std::vector<uint8_t>& mask);
```

Also add `#include <cstdint>` near the top of the header if not already present.

### Step 2 — Implement `forward(mask)` in Bot.cpp

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`. After the existing `bool Bot::forward()` definition (the `}` closing line 291), add:

```cpp
bool Bot::forward(const std::vector<uint8_t>& mask) {
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
            obs_.data(), {1, (int64_t)obs_.size()}, torch::kFloat32).clone();

        auto h = impl_->RunMLP(inp, impl_->shared);
        h = impl_->useLeaky ? torch::leaky_relu(h, 0.01) : torch::relu(h);
        auto logits = impl_->RunMLP(h, impl_->policy);

        auto flat = logits.squeeze().contiguous();
        const int nAct = static_cast<int>(flat.numel());
        if (static_cast<int>(mask.size()) != nAct) {
            // Mask shape mismatch — fall back to unmasked path so we don't go silent.
            return forward();
        }

        // Apply mask: set forbidden logits to -inf so softmax assigns probability 0.
        auto* logitsData = flat.data_ptr<float>();
        bool anyAllowed = false;
        for (int i = 0; i < nAct; ++i) {
            if (mask[i] == 0) {
                logitsData[i] = -std::numeric_limits<float>::infinity();
            } else {
                anyAllowed = true;
            }
        }
        if (!anyAllowed) {
            // Fully masked — emit neutral and bail.
            last_output_ = {};
            last_debug_.available = false;
            last_debug_.action_index = -1;
            return true;
        }

        last_debug_.available = true;
        last_debug_.logits.resize(nAct);
        std::memcpy(last_debug_.logits.data(), flat.data_ptr<float>(),
                    nAct * sizeof(float));

        const int ai = config_.deterministic
            ? static_cast<int>(flat.argmax().item<int64_t>())
            : impl_->Sample(flat);
        last_debug_.action_index = ai;

        const auto& acts = GetDiscreteActions();
        last_output_ = (ai >= 0 && ai < (int)acts.size()) ? acts[ai] : ActionOutput{};
        return true;
    } catch (...) {
        last_output_ = {};
        last_debug_.available = false;
        return true;
    }
}
```

Also add `#include <limits>` near the existing `#include <random>` if not already there.

### Step 3 — Make `BotModule` build the mask and call the new overload

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp`. Find lines 891–916:

```cpp
   
    if (!slot.bot->forward()) {
        if (diag) Console.Write("[GoySDK] S" + std::to_string(slotIdx) + " forward() false");
        return;
    }

    RLInference::ActionOutput action = slot.bot->get_last_output();
    const RLInference::ActionOutput policyAction = action;
    const RLInference::InferenceDebugInfo policyDebug = slot.bot->get_last_debug();

    if (diag) {
        Console.Write("[GoySDK] S" + std::to_string(slotIdx) +
            " fwd OK: actIdx=" + std::to_string(policyDebug.action_index) +
            " logits=" + std::to_string(policyDebug.logits.size()) +
            " thr=" + std::to_string(action.throttle) +
            " str=" + std::to_string(action.steer) +
            " jmp=" + std::to_string(action.jump) +
            " bst=" + std::to_string(action.boost));
    }

    if (policyDebug.available) {
        const int maskedActionIndex = SelectMaskedDiscreteActionIndex(policyDebug.logits, slot.localSnapshot);
        if (maskedActionIndex >= 0 && maskedActionIndex != policyDebug.action_index) {
            action = ToActionOutput(maskedActionIndex);
        }
    }
```

Replace with:

```cpp
    // Build the action mask BEFORE inference so the policy samples in-distribution.
    const std::vector<uint8_t> mask = BuildDefaultActionMask(slot.localSnapshot);

    if (!slot.bot->forward(mask)) {
        if (diag) Console.Write("[GoySDK] S" + std::to_string(slotIdx) + " forward(mask) false");
        return;
    }

    RLInference::ActionOutput action = slot.bot->get_last_output();
    const RLInference::InferenceDebugInfo policyDebug = slot.bot->get_last_debug();

    if (diag) {
        Console.Write("[GoySDK] S" + std::to_string(slotIdx) +
            " fwd OK: actIdx=" + std::to_string(policyDebug.action_index) +
            " logits=" + std::to_string(policyDebug.logits.size()) +
            " thr=" + std::to_string(action.throttle) +
            " str=" + std::to_string(action.steer) +
            " jmp=" + std::to_string(action.jump) +
            " bst=" + std::to_string(action.boost));
    }
    // Mask is now applied inside Bot::forward — no post-hoc fixup needed.
```

### Step 4 — Decide what to do with the now-unused post-hoc helpers

`SelectMaskedDiscreteActionIndex` (in `ActionMask.hpp`) and the `policyAction` local variable are no longer referenced from `RunSlotInferenceTick`. Leave `SelectMaskedDiscreteActionIndex` in place — it's reused if a caller ever needs the deterministic-argmax-over-mask projection. Just drop the local `policyAction` declaration (already removed in the replacement above).

## Verification

1. **Build** — both libraries (`RLInference` and `GoySDKCore`).
2. **Behavioral test (manual)** — set `BotModule::GetSlot(0).config` to use a known model, set `deterministic = false`, force `slot.localSnapshot.boost = 0`. Confirm via the diag log that `actIdx` varies across ticks (proving stochastic sampling is happening) and that `action.boost` is always `false` (proving the mask is applied).
3. **Regression test** — record action distributions over 1000 ticks for a fixed observation before and after the change. Pre-fix: distribution should show "0% mass on masked actions, all collapsed to argmax substitute". Post-fix: distribution should show actual softmax-shaped probabilities over the allowed set.
4. **No silent stub** — confirm that passing a mask of all zeros returns a neutral action AND that the diag prints something distinguishable so it's traceable.

## Don't do

- Do not delete the parameterless `Bot::forward()`. Some test code may depend on it; the new overload is additive.
- Do not implement the mask by post-hoc rejection sampling (sample, retry if invalid). The geometry of softmax over `-inf` logits is exactly what training assumed; rejection sampling is biased toward low-probability allowed actions.
- Do not pre-multiply softmax probabilities by the mask and renormalize. That's mathematically equivalent **only** if no logits are extreme; with float32 and a +20-vs-+30 logit gap, the renormalization underflows. `-inf` on logits is the numerically correct path.
- Do not move `BuildDefaultActionMask` into RLInference. The mask depends on `PlayerSnapshot` which is GoySDK-specific. Keep the boundary clean: GoySDK builds the mask, RLInference applies it.

## Related
- **P0/06** — same fix touches the action-table coupling. Do them together to avoid two passes over the inference code.
- **P2/01** — humanizer deadzone applies after this; consider whether it also needs to be aware of the discrete output set.
