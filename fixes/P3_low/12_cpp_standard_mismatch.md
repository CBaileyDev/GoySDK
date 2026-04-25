# P3 / 12 — CMake says C++17, README says C++20

## TL;DR
`internal_bot/CMakeLists.txt` and `repos/RLInference/CMakeLists.txt` both set C++17:

```cmake
set(CMAKE_CXX_STANDARD 17)
```

But `internal_bot/README.md` says the project requires C++20. The build files are the source of truth. Unless you have a concrete reason to require C++20, the safest fix is to correct the README to C++17.

## Where
- `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt`, current line around 4.
- `/Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt`, current line around 5.
- `/Users/carterbarker/Documents/GoySDK/internal_bot/README.md`, current line around 31.

## Correct Fix Options
Choose one:

- **Option A, recommended:** keep C++17 and update the README.
- **Option B:** intentionally move both CMake projects to C++20 after verifying MSVC, LibTorch, generated SDK headers, Detours, ImGui, and ViGEm all still compile.

## Option A — Keep C++17 and Correct the README
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/README.md`.

Replace:

```md
- C++20, Windows tooling for MSVC / headers
```

with:

```md
- C++17, Windows tooling for MSVC / headers
```

This matches both CMake files and is the lowest-risk correction.

## Option B — Move the Project to C++20
Only choose this if you need a C++20 library feature or language feature.

Edit both:

```text
/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt
/Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt
```

Replace:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

with:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

Then remove or revisit compatibility comments such as:

```cpp
// C++17 compatibility: implement std::lerp and std::midpoint (C++20 features)
```

in `/Users/carterbarker/Documents/GoySDK/internal_bot/Extensions/Extensions/Math.hpp`.

Do not bump only one project. `internal_bot` includes `RLInference` headers and links the library; the standards should agree unless there is a deliberate reason for mismatch.

## Verification
### If Option A
1. Build as-is.
2. Confirm README says C++17.
3. Run:
   ```bash
   rg -n "C\\+\\+20|CMAKE_CXX_STANDARD" /Users/carterbarker/Documents/GoySDK/internal_bot /Users/carterbarker/Documents/GoySDK/repos/RLInference
   ```
   Expected:
   - README no longer claims C++20 as a requirement.
   - Both CMake files still set 17.

### If Option B
1. Build `RLInference`.
2. Build `GoySDKCore`.
3. Verify generated SDK headers still compile under MSVC C++20.
4. Verify LibTorch headers do not produce new C++20-mode warnings/errors.
5. Verify no compatibility helpers now conflict with standard library definitions.

## Don't Do
- Do not update only README examples while leaving the requirement line wrong.
- Do not bump only `internal_bot` or only `RLInference`.
- Do not choose C++20 just because it is newer; use it only if the code needs it and the toolchain has been verified.

## Related
- **P3/11** — another CMake cleanup in the same files.
