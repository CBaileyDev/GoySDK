# P3 / 03 — `Core.InitializeGlobals` cast to `LPTHREAD_START_ROUTINE`

## TL;DR
`dllmain.cpp` does:
```cpp
CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
```
This works because `Core.InitializeGlobals` happens to compile to a free-function-shaped pointer in the `Components/Components/Core` module, but the syntax `Core.InitializeGlobals` *looks* like it's binding a member function pointer, which would be UB to cast to `LPTHREAD_START_ROUTINE`. Even when correct, the line is fragile to refactors.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/dllmain.cpp`
- Line: **17**

```cpp
CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
```

## Problem
1. If `Core` is an instance and `InitializeGlobals` is a non-`static` member function, taking its address with `Core.InitializeGlobals` (no `&`) is non-standard syntax and may not even produce a usable function pointer; the cast then hides the error and produces UB.
2. If `Core` is an instance but `InitializeGlobals` is `static`, the syntax works and is portable, but the `Core.` prefix is misleading.
3. If `Core` is a `struct` (effectively a namespace) the code works but again the syntax suggests otherwise.
4. `CreateThread` is called from inside `DllMain`. The callback runs while the loader lock is potentially held — `InitializeGlobals` must not call `LoadLibrary`, `GetModuleHandle`, or anything that touches DLL init.

Verify what `Core.InitializeGlobals` actually is:
```bash
grep -n "InitializeGlobals" /Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/Core.cpp /Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/Core.hpp
```

## Fix

### Step 1 — Confirm `InitializeGlobals` is a free function or static method

After running the grep above:
- If it's a **free function** in a namespace `Core`, change the call to `&Core::InitializeGlobals` (no instance access).
- If it's a **static method** of class `Core`, change to `&Core::InitializeGlobals`.
- If it's a **non-static method**, see "Plan B" below.

For the most common case (static method or namespace function), edit `/Users/carterbarker/Documents/GoySDK/internal_bot/dllmain.cpp`. Find:

```cpp
CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
```

Replace with:

```cpp
HANDLE hThread = CreateThread(
    nullptr, 0,
    [](LPVOID lpParam) -> DWORD {
        // Trampoline: run global init off the DllMain thread.
        Core::InitializeGlobals(static_cast<HMODULE>(lpParam));
        return 0;
    },
    hModule, 0, nullptr);
if (hThread) {
    CloseHandle(hThread);
}
```

(The lambda has no captures, so it implicitly converts to `LPTHREAD_START_ROUTINE` — no `reinterpret_cast` needed.)

### Plan B — `InitializeGlobals` is a non-static member

Add a free-function trampoline near the top of `dllmain.cpp`:

```cpp
static DWORD WINAPI InitializeGlobalsTrampoline(LPVOID lpParam) {
    Core.InitializeGlobals(static_cast<HMODULE>(lpParam));
    return 0;
}
```

Then call:
```cpp
CreateThread(nullptr, 0, InitializeGlobalsTrampoline, hModule, 0, nullptr);
```

### Step 2 — Don't leak the thread handle

The original code didn't capture or close the handle returned by `CreateThread`. The lambda version above closes it. If you used Plan B, add the same `CloseHandle`:

```cpp
HANDLE hThread = CreateThread(nullptr, 0, InitializeGlobalsTrampoline, hModule, 0, nullptr);
if (hThread) CloseHandle(hThread);
```

### Step 3 — Verify the callback is safe to run during DLL_PROCESS_ATTACH

`InitializeGlobals` must not block on the loader lock. Audit the function for `LoadLibrary*`, `GetModuleHandle*` (calling `GetProcAddress` is fine), `CoInitialize`, and similar. If it does any of those, defer them to the first frame after init by setting a flag and processing it from a hooked tick.

## Verification

1. **Build** — must compile without `reinterpret_cast` warnings.
2. **Inject** — confirm the bot still loads and `Console.Write("[GoySDK] Initializing...")` appears.
3. **Static check** — `grep -n "reinterpret_cast<LPTHREAD_START_ROUTINE>" /Users/carterbarker/Documents/GoySDK/internal_bot/dllmain.cpp` should be empty.

## Don't do

- Do not remove the `CreateThread` and call `Core.InitializeGlobals(hModule)` directly inside `DllMain`. That makes init run while the loader lock is held — guaranteed deadlock for any reasonable init path.
- Do not pass `nullptr` for `lpParameter` — the function needs the `HMODULE` to set the DLL search directory, etc.
