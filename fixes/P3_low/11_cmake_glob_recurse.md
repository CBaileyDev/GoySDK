# P3 / 11 — `GLOB_RECURSE` does not reconfigure automatically for new files

## TL;DR
Both CMake projects use `file(GLOB_RECURSE ...)` without `CONFIGURE_DEPENDS`:

- `internal_bot/CMakeLists.txt`
- `repos/RLInference/CMakeLists.txt`

Without `CONFIGURE_DEPENDS`, adding a new `.cpp` file may not trigger CMake reconfiguration, so the new source is not compiled until someone manually reruns CMake.

Fix either by adding `CONFIGURE_DEPENDS` to the globs or by replacing globs with explicit source lists. For this repo, `CONFIGURE_DEPENDS` is the pragmatic low-risk fix.

## Where
- `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt`, current lines around 22-34.
- `/Users/carterbarker/Documents/GoySDK/repos/RLInference/CMakeLists.txt`, current line around 10.
- Both projects use `cmake_minimum_required(VERSION 3.18)`, which supports `CONFIGURE_DEPENDS`.

## Correct Fix Strategy
Use `CONFIGURE_DEPENDS` for the existing glob patterns. This preserves the current project style while making new files visible on normal builds.

## Step 1 — Update `internal_bot/CMakeLists.txt`
Replace:

```cmake
file(GLOB_RECURSE TNTS_SOURCES
    "Components/*.cpp"
    "Modules/*.cpp"
    "Extensions/*.cpp"
    "ImGui/*.cpp"
    "RLSDK/*.cpp"
    "OverlayRenderer.cpp"
    "pch.cpp"
)

file(GLOB_RECURSE GOYSDK_SOURCES
    "GoySDK/*.cpp"
)
```

with:

```cmake
file(GLOB_RECURSE TNTS_SOURCES CONFIGURE_DEPENDS
    "Components/*.cpp"
    "Modules/*.cpp"
    "Extensions/*.cpp"
    "ImGui/*.cpp"
    "RLSDK/*.cpp"
    "OverlayRenderer.cpp"
    "pch.cpp"
)

file(GLOB_RECURSE GOYSDK_SOURCES CONFIGURE_DEPENDS
    "GoySDK/*.cpp"
)
```

## Step 2 — Update `repos/RLInference/CMakeLists.txt`
Replace:

```cmake
file(GLOB_RECURSE RLINFERENCE_SOURCES "src/*.cpp")
```

with:

```cmake
file(GLOB_RECURSE RLINFERENCE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
```

If **P0/01** vendors `miniz`, the RLInference source list may also explicitly add `third_party/miniz/miniz.c`. Do that in `add_library`, not by broadening the glob to all third-party files.

## Step 3 — Alternative: Explicit Source Lists
If the project wants reproducible source declarations instead of globs, replace the globs with explicit lists. That is more work but valid.

Generate a starting list with:

```bash
find /Users/carterbarker/Documents/GoySDK/internal_bot \
  -path '*/build' -prune -o \
  -name '*.cpp' -print | sort
```

Then remove generated/vendor files you do not want compiled directly.

Do not mix a large explicit list with a broad glob for the same directories. Pick one style.

## Verification
1. Configure/build normally.

2. Add a temporary source file:
   ```text
   /Users/carterbarker/Documents/GoySDK/internal_bot/GoySDK/__cmake_glob_test.cpp
   ```
   with:
   ```cpp
   namespace { int cmake_glob_test_symbol = 0; }
   ```

3. Build again.
   Expected: CMake notices glob mismatch and re-runs configure before compiling.

4. Delete the temporary file and build again.
   Expected: CMake re-runs configure and removes it from the target.

5. Repeat for:
   ```text
   /Users/carterbarker/Documents/GoySDK/repos/RLInference/src/__cmake_glob_test.cpp
   ```

## Don't Do
- Do not use `aux_source_directory`; it has the same class of problem.
- Do not add `CONFIGURE_DEPENDS` to vendor directories broadly. Keep third-party sources explicit.
- Do not leave RLInference unchanged; it has the same issue as `internal_bot`.

## Related
- **P0/01** — if `miniz` is vendored, wire it explicitly while touching `repos/RLInference/CMakeLists.txt`.
