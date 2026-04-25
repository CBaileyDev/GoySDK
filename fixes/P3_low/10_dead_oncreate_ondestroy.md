# P3 / 10 — Empty `OnCreate`/`OnDestroy` in OverlayRenderer

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Lines: **484–490**

```cpp
void OverlayRenderer::OnCreate() {
   
}

void OverlayRenderer::OnDestroy() {
   
}
```

## Problem
The base `Module` class likely declares `OnCreate`/`OnDestroy` as virtual hooks. The overrides here do nothing. Either there's missing intended cleanup, or the overrides should be removed.

## Fix

### Option A (recommended) — Remove the overrides

If `Module::OnCreate`/`OnDestroy` are virtual with empty defaults, the overrides add nothing.

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`. Find lines 484–490:

```cpp
void OverlayRenderer::OnCreate() {
   
}

void OverlayRenderer::OnDestroy() {
   
}
```

Delete both functions.

Then edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`. Find the declarations of `OnCreate` and `OnDestroy` and remove them.

Then check the constructor/destructor at lines 479–482:
```cpp
OverlayRenderer::OverlayRenderer() : Module("GameEventHook", "Hooks into game events", States::STATES_All) {
    OnCreate();
}
OverlayRenderer::~OverlayRenderer() { OnDestroy(); }
```

If you removed the overrides, these calls now resolve to the base class's empty `Module::OnCreate`/`OnDestroy`. If the base class's are also empty no-ops, you can remove the explicit calls. Otherwise leave them.

### Option B — Move existing init/teardown into them

If the constructor/destructor currently do work that *should* be in `OnCreate`/`OnDestroy` (e.g., if the convention is "constructor only initializes, OnCreate registers"), leave the functions in place but populate them. Look at how `BotModule::OnCreate` (line 199 of BotModule.cpp) is structured — it does padState init. The overlay equivalent would be initializing render-state defaults.

For OverlayRenderer there isn't anything obvious to move, so Option A is the right call.

## Verification
- Build. No `Module::OnCreate is hidden` warnings.
- Inject; overlay still renders.

## Don't do
- Don't add `// TODO: implement` comments and leave them empty. That's worse than removing — it suggests something needs to be done that nobody has scoped.

## Related
None.
