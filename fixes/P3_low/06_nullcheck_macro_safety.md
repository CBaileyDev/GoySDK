# P3 / 06 — `nullcheck` macro unused, lacks `do { } while(0)` wrapping

## TL;DR
`pch.hpp` defines a `nullcheck(ptr)` macro that early-`return`s if `ptr` is null. The macro is never used anywhere in the GoySDK source. The macro definition itself isn't wrapped in `do { } while(0)`, so any conditional usage like `if (cond) nullcheck(p); else foo();` would silently misparse the `else`. Either delete the macro or fix the wrapping.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/pch.hpp`
- Line: **4**

```cpp
#define nullcheck(ptr) if (ptr == NULL || ptr == nullptr || !ptr) {return;}
```

Verified zero usages elsewhere:
```bash
grep -rn "nullcheck" /Users/carterbarker/Documents/GoySDK/internal_bot/ --include="*.cpp" --include="*.hpp"
```
Returns only the definition.

## Problem
1. The macro is dead.
2. If anyone *did* use it inside an `if`/`else`, the dangling `else` would attach to the macro's internal `if`, not the outer `if` the user wrote. Classic C macro footgun.
3. The "triple-redundant" check `ptr == NULL || ptr == nullptr || !ptr` is overkill for raw pointers (where all three are equivalent) and incorrect for pointer-like types (where `ptr == NULL` may not compile). Either way, lazy.

## Why it matters
Future maintainer sees the macro, decides to use it, walks into the dangling-`else` trap. Easier to delete now.

## Fix

### Option A (recommended) — Delete the macro

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/pch.hpp`. Find:
```cpp
#define nullcheck(ptr) if (ptr == NULL || ptr == nullptr || !ptr) {return;}
```

Delete the line entirely.

### Option B — Keep the macro but wrap it safely

If for some reason the macro must stay, replace with:
```cpp
#define nullcheck(ptr) do { if (!(ptr)) { return; } } while (0)
```

Notes:
- The `(ptr)` parens are required so `nullcheck(a + b)` doesn't expand to `if (!a + b)`.
- `!(ptr)` covers all pointer-like types via implicit bool conversion; the triple-redundant check is unnecessary.
- The `do { ... } while (0)` makes the macro a single statement, so `if (cond) nullcheck(p); else foo();` parses as intended.

## Verification

### If Option A
- Build with `-Werror=unused-macros` or `/W4` — confirm no warnings.
- `grep -rn "nullcheck" /Users/carterbarker/Documents/GoySDK/internal_bot/` returns no hits.

### If Option B
- Write a tiny test:
  ```cpp
  void f(int* p) { if (true) nullcheck(p); else std::abort(); }
  ```
- Confirm it compiles and that calling `f(nullptr)` returns without aborting.

## Don't do
- Don't keep the macro as-is "in case." Dead, footgun-shaped code is worse than no code.
- Don't replace `return;` with `return false;` or other return values; the early-return type is determined by the function the macro is called from. The original is correct only for `void`-returning functions.

## Related
None.
