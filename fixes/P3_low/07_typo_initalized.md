# P3 / 07 — "Initalized" typo in OverlayRenderer log

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/OverlayRenderer.cpp`
- Line: **494**

```cpp
Console.Write("OverlayRenderer Initalized.");
```

## Fix

Edit the file. Replace:
```cpp
Console.Write("OverlayRenderer Initalized.");
```
with:
```cpp
Console.Write("OverlayRenderer Initialized.");
```

(Single letter: `Initalized` → `Initialized`.)

## Verification
- Build.
- Inject; confirm the log line now reads `OverlayRenderer Initialized.`

## Don't do
- Don't grep-and-replace `Initalized` everywhere unconditionally — `Functions.hpp` and `Offsets.hpp` contain identical typos (`PrintSeperater`, `Recieved`) but those are **generated** from upstream RL function names; changing them breaks string-name lookups against the live game. Touch only the GoySDK-authored source.

## Related
None.
