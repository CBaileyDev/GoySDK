# P3 / 12 — CMake says C++17, README says C++20

## TL;DR
`CMakeLists.txt:4` sets `CMAKE_CXX_STANDARD 17`. `README.md:31` states the project requires C++20. Pick one and reconcile.

## Where
- `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt:4`
  ```cmake
  set(CMAKE_CXX_STANDARD 17)
  ```
- `/Users/carterbarker/Documents/GoySDK/internal_bot/README.md:31`
  ```
  - C++20, Windows tooling for MSVC / headers
  ```

## Problem
The CMake settings determine what features actually compile. The README determines what the contributor reads. If a contributor uses a C++20-only feature (concepts, `std::span`, `<format>`, designated initializers in some forms) believing the README, the build breaks.

## Fix

### Option A (recommended) — Move to C++20 (matches README)

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt`. Find:
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

Replace with:
```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

Verify the same standard is set on the `RLInference` subproject:
```bash
grep -n "CMAKE_CXX_STANDARD" /Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt
```
If it's 17 or unset, bump to 20 there too.

Verify the project compiles. LibTorch supports C++17 and C++20; ViGEmClient is C-style and indifferent.

### Option B — Stay on C++17 (correct the README)

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/README.md`. Find:
```
- C++20, Windows tooling for MSVC / headers
```

Replace with:
```
- C++17, Windows tooling for MSVC / headers
```

## Verification
- Build. Confirm no errors.
- Optional: write a small `static_assert(__cplusplus >= 202002L);` (C++20) or `>= 201703L` (C++17) in `pch.hpp` to enforce the chosen standard at compile time.

## Don't do
- Don't pick a higher standard than your toolchain supports. MSVC's C++20 support is mostly there but check if you're on an older compiler.
- Don't bump only one of the two CMakeLists. The subproject must agree, otherwise inter-library headers can hit ABI issues at boundary types.

## Related
None.
