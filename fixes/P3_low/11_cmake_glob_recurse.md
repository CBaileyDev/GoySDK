# P3 / 11 — `GLOB_RECURSE` doesn't trigger reconfigure on new files

## TL;DR
`internal_bot/CMakeLists.txt` uses `file(GLOB_RECURSE TNTS_SOURCES "Components/*.cpp" ...)`. CMake's documentation explicitly warns against this — adding a new `.cpp` file does not trigger re-configuration, so the new source isn't compiled until the user manually reruns CMake. Switch to explicit source lists or use `CONFIGURE_DEPENDS`.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt`
- Lines: **22–34**

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

## Problem
- New `.cpp` file under `Components/` → CMake's generated build system has no rule for it until you manually rerun CMake.
- Symptom: "I added MyMod.cpp but it's not being compiled / not being linked."
- The `GLOB` form has the same problem; `GLOB_RECURSE` adds the additional pitfall that nested directories are walked silently.

## Fix

Two valid approaches.

### Option A (recommended) — `CONFIGURE_DEPENDS`

CMake 3.12+ supports `CONFIGURE_DEPENDS` which makes the generator re-glob at every build, so new files are picked up automatically.

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/CMakeLists.txt`. Find:
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

Replace with:
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

Verify `cmake_minimum_required(VERSION 3.18)` (line 1) is ≥ 3.12 — yes, 3.18 supports `CONFIGURE_DEPENDS`.

### Option B — Explicit source list

If the project values reproducible builds across CMake versions, list every file explicitly. More maintenance overhead but bulletproof:

```cmake
set(TNTS_SOURCES
    Components/Component.cpp
    Components/Components/Core.cpp
    Components/Components/Console.cpp
    Components/Components/Events.cpp
    Components/Components/GameState.cpp
    Components/Components/GUI.cpp
    Components/Components/Instances.cpp
    Components/Components/Main.cpp
    Components/Components/Manager.cpp
    Extensions/Extensions/Colors.cpp
    Extensions/Extensions/Formatting.cpp
    Extensions/Extensions/Math.cpp
    Extensions/Extensions/Memory.cpp
    Extensions/Extensions/UnrealMemory.cpp
    Modules/Module.cpp
    Modules/Mods/Drawing.cpp
    OverlayRenderer.cpp
    pch.cpp
    # add ImGui and RLSDK files explicitly too — generate the list with `ls`
)

set(GOYSDK_SOURCES
    GoySDK/BotModule.cpp
    GoySDK/ObsBuilder.cpp
    GoySDK/ViGemController.cpp
)
```

(Generate the full list with `find Components Modules Extensions ImGui RLSDK GoySDK -name '*.cpp' | sort`.)

## Verification

### If Option A
- Add a new file `Components/Components/Test.cpp` containing `// test`.
- Build. Confirm CMake re-globs (you'll see `Re-running CMake...` or similar).
- Confirm the new file is listed in the build output.
- Delete the test file when done.

### If Option B
- Build. Confirm every existing source still compiles.
- Try adding a new `.cpp` without updating the list — confirm the build doesn't pick it up (this is the trade-off of explicit lists).

## Don't do
- Don't switch to `aux_source_directory` — it's the original CMake glob and has all the same problems.
- Don't keep `GLOB_RECURSE` without `CONFIGURE_DEPENDS` "because it works in our CI." The first time someone adds a file locally, they'll waste time debugging "why isn't this compiling."
