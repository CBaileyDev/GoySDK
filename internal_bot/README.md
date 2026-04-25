# TNTs-Template

### Unreal Engine 3 mod template (build **Release** / **x64**)

`DISCLAIMER: IF YOU USE THIS PROJECT FOR MALICIOUS PURPOSES THAT IS ON YOU. THIS PROJECT IS FOR MODDING / RESEARCH IN CONTROLLED ENVIRONMENTS.`

- Based on a public CodeRed-style UE3 mod template, with ImGui via Kiero-style hooking.
- Pattern scanning for globals follows the same ideas as common UE3 dump/scan projects (no vendor-specific claim).

The Visual Studio “filters” (virtual folders) may not match the filesystem; check **Additional include directories** if includes fail.

### Finding functions

Use the included generated headers and your engine’s `FindFunction` / `ProcessEvent` flow, or a script that scans the target image for the usual GNames / GObjects array patterns (see the loader’s offset script).

To refresh headers when the game updates, use an SDK generator that supports your build’s UDK / UE3 layout.

A simple world-to-screen helper is included in the drawing module.

```cpp
Main.Execute([]() { /* your call */ });
```

## CodeRed-style feature overview (v1.3.9)

Instance storage, game-state tracking, and hooks are still **title-specific**; extend them for the classes and events your build uses. Comments in the tree explain the patterns.

#### Requirements

- Unreal concepts: globals, objects, reflection, UFunction / ProcessEvent
- C++17, Windows tooling for MSVC / headers
- Detours (see `https://github.com/microsoft/Detours/`)

## Features

- Debug console and optional log file
- Global init and ProcessEvent detour
- Pre/post UFunction hook registration
- Instance cache (static and dynamic) for `UObject` / `A*` singletons
- Command / config / “mod” registry for ImGui and console
- `GameState` example tracker

## License

See the project license.

## Screenshot (theme may vary)

![](https://i.imgur.com/ofnaNVV.png)
