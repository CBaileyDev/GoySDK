# P3 / 03 — `Core.InitializeGlobals` is cast to `LPTHREAD_START_ROUTINE`

## TL;DR
`internal_bot/dllmain.cpp` starts initialization with:

```cpp
CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
```

`Core.InitializeGlobals` currently resolves to a static member function declared as:

```cpp
static void InitializeGlobals(HMODULE hModule);
```

The code happens to compile, but it is fragile and misleading:

- It uses instance-style syntax for a static method.
- It casts a `void(HMODULE)` function to a `DWORD WINAPI(LPVOID)` thread procedure.
- It leaks the thread handle returned by `CreateThread`.

Fix with an explicit thread trampoline whose signature actually matches `LPTHREAD_START_ROUTINE`.

## Where
- Thread creation: `/Users/carterbarker/Documents/GoySDK/internal_bot/dllmain.cpp`, current line around 17.
- Static method declaration: `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/Core.hpp`, current line around 22.
- Static method definition: `/Users/carterbarker/Documents/GoySDK/internal_bot/Components/Components/Core.cpp`, current line around 100.

## Correct Fix Strategy
Do not call `InitializeGlobals` directly from `DllMain`. Keep initialization on a separate thread, but use a correctly typed trampoline and close the thread handle.

## Step 1 — Add a Trampoline in `dllmain.cpp`
Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/dllmain.cpp`.

Above `DllMain`, add:

```cpp
static DWORD WINAPI InitializeGlobalsThread(LPVOID param)
{
    CoreComponent::InitializeGlobals(static_cast<HMODULE>(param));
    return 0;
}
```

`CoreComponent::InitializeGlobals` is the actual static method name. Do not use `Core::InitializeGlobals`; `Core` is the global instance name, not the class.

## Step 2 — Replace the Casted `CreateThread`
Replace:

```cpp
CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Core.InitializeGlobals), hModule, 0, nullptr);
```

with:

```cpp
HANDLE initThread = CreateThread(nullptr, 0, InitializeGlobalsThread, hModule, 0, nullptr);
if (initThread) {
    CloseHandle(initThread);
}
```

Closing the handle does not stop the thread. It only releases this DLL's handle reference.

## Step 3 — Optional: Fix Similar Pattern in `CoreComponent::InitializeThread`
`Core.cpp` currently has:

```cpp
MainThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(InitializeGlobals), nullptr, 0, nullptr);
```

This is the same signature bug, and worse, it passes `nullptr` where `InitializeGlobals` expects an `HMODULE`. If this method is unused, either delete it or update it to use the same trampoline pattern with a real module handle.

Recommended minimal change:

```cpp
void CoreComponent::InitializeThread()
{
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&CoreComponent::InitializeGlobals),
        &module);

    MainThread = CreateThread(nullptr, 0, InitializeGlobalsThread, module, 0, nullptr);
}
```

However, `InitializeGlobalsThread` is currently in `dllmain.cpp`, so this exact reuse requires moving the trampoline to a shared file or adding a second local trampoline in `Core.cpp`. If `InitializeThread()` is not called anywhere, leave it for a separate cleanup.

## Step 4 — Loader-Lock Reminder
This fix keeps the current behavior of starting a thread from `DllMain`. That is a common pattern, but the new thread may begin running before `DllMain` returns. `InitializeGlobals` calls functions like `GetModuleFileNameA`, initializes logging, scans modules, and hooks systems. If you observe loader-lock deadlocks, the deeper fix is to defer heavy initialization until after process attach returns.

Do not broaden this P3 cleanup into an initialization redesign unless you are actively debugging a deadlock.

## Verification
1. Build `GoySDKCore`.

2. Static checks:
   ```bash
   rg -n "reinterpret_cast<LPTHREAD_START_ROUTINE>|Core\\.InitializeGlobals|Core::InitializeGlobals" /Users/carterbarker/Documents/GoySDK/internal_bot
   ```
   Expected for `dllmain.cpp`:
   - no `reinterpret_cast<LPTHREAD_START_ROUTINE>`,
   - no `Core.InitializeGlobals`,
   - no `Core::InitializeGlobals`.

3. Inject/run:
   - Confirm `[Core Module] Initializing globals...` still appears.
   - Confirm no thread-handle leak is reported by diagnostics.

## Don't Do
- Do not call `CoreComponent::InitializeGlobals(hModule)` directly inside `DllMain`.
- Do not use `Core::InitializeGlobals`; `Core` is not the class name.
- Do not pass `nullptr` as the module handle.
- Do not keep the reinterpret cast just because it compiles.
