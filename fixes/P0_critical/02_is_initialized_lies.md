# P0 / 02 — `Bot::is_initialized()` always returns true

## TL;DR
`Bot::is_initialized()` returns `impl_ != nullptr`, which is always true after construction (the `unique_ptr<Impl>` is created unconditionally). Callers that check `is_initialized()` to detect "model loaded" never see failure.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`
- Function: `Bot::is_initialized`
- Lines: **241–244**

Existing code:
```cpp
bool Bot::is_initialized() const {
    // Always return true — even if model loading failed we still work (stub).
    return impl_ != nullptr;
}
```

## Problem
The comment is honest, but the contract the comment encodes (`is_initialized` ≠ "loaded successfully") is the **opposite** of what every caller assumes:

- `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp:288` — used immediately after `make_unique<RLInference::Bot>` to detect load failure. Always passes.
- `/Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/BotModule.cpp:822` — gates `RunSlotInferenceTick`. Always passes, so a stub bot still ticks every frame and emits neutral output.

This is the propagating mechanism behind **P0/01**.

## Why it matters
Even after fixing the DEFLATE parser, any *future* failure mode (corrupt resource, mismatched layer sizes, dimension mismatch, throw inside `LoadMLP`) will be invisible to the host application. The only signal a user has that something is wrong is "the bot stands still."

## Root cause
Author optimized for "never crash" by always taking the stub-mode branch in `forward()`. They added `is_initialized()` as a hedge against `impl_` being null, then never wired it to the actual `ready` flag.

## Fix

### Step 1 — Make `is_initialized()` honest

Edit `/Users/carterbarker/Documents/GoySDK/repos/RLInference/src/Bot.cpp`. Find:
```cpp
bool Bot::is_initialized() const {
    // Always return true — even if model loading failed we still work (stub).
    return impl_ != nullptr;
}
```

Replace with:
```cpp
bool Bot::is_initialized() const {
    // True only if the model loaded successfully. Stub-mode bots return false
    // so callers can distinguish "ready to infer" from "constructed but inert".
    return impl_ != nullptr && impl_->ready;
}
```

### Step 2 — Audit `forward()` callers

`Bot::forward` already handles the `!impl_->ready` case internally (it returns `true` after writing a neutral output). After Step 1, the `slot.bot && slot.bot->is_initialized()` gate at `BotModule.cpp:822` will skip the call entirely for stub bots — which is correct, because there's nothing to infer.

But `BotModule::LoadBotForSlot` at `BotModule.cpp:288` was already checking `is_initialized()` after construction. Currently this never trips. **After Step 1, it will start tripping correctly** and route through the existing error log + cleanup at lines 289–298. No change needed at the call site, but verify that path during testing.

### Step 3 — Add a separate `is_constructed()` if any caller actually needs the old behavior

Search the codebase first:
```bash
grep -rn "is_initialized" /Users/carterbarker/Documents/GoySDK/internal_bot/ /Users/carterbarker/Documents/GoySDK/repos/
```
Both call sites in BotModule.cpp expect "loaded". No other production callers exist. **Skip Step 3.** If a future caller needs to check "object exists but model failed," add an `is_stub()` accessor then.

## Verification

1. **Compile** — `cd /Users/carterbarker/Documents/GoySDK/internal_bot && cmake --build build` (or whatever the project build command is). Fix any new errors that surface from callers depending on the old contract.
2. **Negative test** — temporarily set `impl_->ready = false` at the end of the constructor. Inject. Confirm the console prints `[GoySDK] Slot 0: Bot created but model initialization FAILED for ABUSE` and that `botActive_` cannot be toggled to a useful state for that slot.
3. **Positive test** — revert the negative test. Confirm normal model loads still print `[GoySDK] Slot 0: Loaded ABUSE` and the bot ticks.

## Don't do

- Do not delete the stub-mode branch in `forward()`. It's still needed as a safety net for the (rare) case where `impl_->ready` somehow flips after construction (it shouldn't, but the cost is one boolean check).
- Do not change the constructor to throw on failure. The current "no-throw, set ready=false" pattern is good — it just needs `is_initialized` to expose the truth.
- Do not rename `is_initialized` to `is_loaded` or `is_ready`. The semantic change is enough; renaming forces every caller in the codebase to be touched and is just churn.

## Related
- **P0/01** — fix together; this surfaces the errors that one will produce.
