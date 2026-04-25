# P3 / 10 — Empty `OverlayRenderer::OnCreate` / `OnDestroy`

## TL;DR
`OverlayRenderer` defines empty `OnCreate` and `OnDestroy` methods and calls them from its constructor/destructor:

```cpp
OverlayRenderer::OverlayRenderer() : Module(...) {
    OnCreate();
}
OverlayRenderer::~OverlayRenderer() { OnDestroy(); }

void OverlayRenderer::OnCreate() {}
void OverlayRenderer::OnDestroy() {}
```

The previous note said those calls would resolve to base-class methods if the overrides were removed. That is wrong for the current tree: `Module` does not declare `OnCreate` or `OnDestroy`. If you remove the functions but leave the calls, the code will not compile.

Fix by deleting both the empty methods **and** the constructor/destructor calls.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Constructor/destructor: current lines around 479-482.
- Empty functions: current lines around 484-490.
- Declarations: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`, current lines around 38-39.
- Base class: `/Users/carterbarker/Documents/GoySDK/internal_bot/Modules/Module.hpp`; no `OnCreate` / `OnDestroy` virtuals exist there.

## Correct Fix
Remove the dead hooks completely.

## Step 1 — Update Header
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.hpp`.

Delete:

```cpp
void OnCreate();
void OnDestroy();
```

## Step 2 — Update Constructor and Destructor
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`.

Replace:

```cpp
OverlayRenderer::OverlayRenderer() : Module("GameEventHook", "Hooks into game events", States::STATES_All) {
    OnCreate();
}
OverlayRenderer::~OverlayRenderer() { OnDestroy(); }
```

with:

```cpp
OverlayRenderer::OverlayRenderer()
    : Module("GameEventHook", "Hooks into game events", States::STATES_All) {}

OverlayRenderer::~OverlayRenderer() = default;
```

If your compiler rejects an out-of-line defaulted destructor because of the existing declaration, use:

```cpp
OverlayRenderer::~OverlayRenderer() {}
```

but prefer `= default`.

## Step 3 — Delete Empty Definitions
Delete:

```cpp
void OverlayRenderer::OnCreate() {

}

void OverlayRenderer::OnDestroy() {

}
```

## Step 4 — Confirm No Other Calls Exist
Run:

```bash
rg -n "OverlayRenderer::OnCreate|OverlayRenderer::OnDestroy|OnCreate\\(\\)|OnDestroy\\(\\)" /Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.*
```

Expected:
- no `OverlayRenderer::OnCreate`,
- no `OverlayRenderer::OnDestroy`,
- no constructor/destructor calls to them.

## Verification
1. Build `GoySDKCore`.
2. Inject/run.
3. Confirm `OverlayRenderer::Initialize()` still calls `Hook()` and logs initialization.
4. Confirm overlay rendering behavior is unchanged.

## Don't Do
- Do not remove only the function definitions and leave constructor/destructor calls.
- Do not add `// TODO` empty hooks.
- Do not assume `Module` provides fallback hook methods; it does not in this tree.

## Related
None.
