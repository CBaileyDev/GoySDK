# P2 / 03 — `InjectLoadLibrary` returns 32-bit truncated HMODULE on x64

## TL;DR
`DllInjector.InjectLoadLibrary` calls `GetExitCodeThread` to read `LoadLibraryW`'s return value. On x64, thread exit codes are still 32-bit (`DWORD`), but `HMODULE` is 64-bit. The returned `IntPtr` therefore contains only the **lower 32 bits** of the loaded module's base address. The current code only checks `exitCode == IntPtr.Zero`, which misfires only when the lower 32 bits happen to be zero — but any caller that treats the return value as a real `HMODULE` (e.g., to call `FreeLibrary` later) will hit an access violation.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`
- Function: `InjectLoadLibrary`
- Lines: **47–58**

```csharp
hThread = NativeMethods.CreateRemoteThread(hProcess, IntPtr.Zero, 0, loadLib, remoteMem, 0, out _);
if (hThread == IntPtr.Zero)
    throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateRemoteThread failed.");

var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
if (wait != 0)
    throw new TimeoutException("Remote LoadLibraryW did not complete in time.");

if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode) || exitCode == IntPtr.Zero)
    throw new InvalidOperationException("LoadLibraryW returned NULL in the target process (missing dependencies or wrong architecture).");

return exitCode;
```

## Problem
1. The return value is documented (implicitly, by being typed `IntPtr`) as the loaded module handle. On x64 it's not — it's a useless truncation.
2. The "load failed" detection (`exitCode == IntPtr.Zero`) misses load failures whose bottom 32 bits aren't zero. While `LoadLibraryW` returning NULL is the standard failure indicator, on truly catastrophic failures (e.g., the remote thread crashing before it returns), `GetExitCodeThread` may return a non-zero garbage value.
3. Callers can't reliably use the return for anything.

## Why it matters
Today, the only caller (`MainWindow.xaml.cs`) discards the return value, so this bug is latent. The moment someone adds an "unload bot" feature that calls `FreeLibrary`, they'll either crash the host or leak the module forever.

## Root cause
.NET P/Invoke convenience: `GetExitCodeThread`'s `LPDWORD` parameter is bound to `out IntPtr` for "looks like a pointer." But the underlying API really does only write 32 bits regardless of platform.

## Fix

The standard pattern is to call `GetModuleHandleW` from a *second* remote thread, this time with the DLL path (still in remote memory from the first call). That returns a full HMODULE.

### Step 1 — Implement a remote `GetModuleHandleW` follow-up

Add a private helper to `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`. Insert this method just after `InjectLoadLibrary`:

```csharp
private static IntPtr ResolveRemoteHModule(IntPtr hProcess, IntPtr remoteDllPathW)
{
    var k32 = NativeMethods.GetModuleHandleW("kernel32.dll");
    if (k32 == IntPtr.Zero) return IntPtr.Zero;

    var getModuleHandle = NativeMethods.GetProcAddress(k32, "GetModuleHandleW");
    if (getModuleHandle == IntPtr.Zero) return IntPtr.Zero;

    var hThread = NativeMethods.CreateRemoteThread(
        hProcess, IntPtr.Zero, 0, getModuleHandle, remoteDllPathW, 0, out _);
    if (hThread == IntPtr.Zero) return IntPtr.Zero;

    try
    {
        var wait = NativeMethods.WaitForSingleObject(hThread, 5_000);
        if (wait != 0) return IntPtr.Zero;

        // Same caveat as LoadLibraryW: this truncates on x64. So we use it only
        // to confirm "module is loaded" (non-zero), not as the handle itself.
        if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode))
            return IntPtr.Zero;
        return exitCode;
    }
    finally
    {
        NativeMethods.CloseHandle(hThread);
    }
}
```

Wait — the same truncation applies to `GetModuleHandleW` over the thread-exit-code channel. The proper resolution is to **enumerate the target's modules from this side** using `EnumProcessModulesEx` after the inject completes.

### Step 2 — Use `EnumProcessModulesEx` to resolve the real HMODULE

Replace the `return exitCode;` line in `InjectLoadLibrary` (line 58) with:

```csharp
            // GetExitCodeThread truncates HMODULE to 32 bits on x64, so its
            // return is only useful as a "non-zero == load succeeded" signal.
            // Resolve the actual module handle by enumerating the target's
            // modules and matching by file name.
            var moduleFileName = Path.GetFileName(absoluteDllPath);
            var realHandle = FindModuleHandle(processId, moduleFileName);
            if (realHandle == IntPtr.Zero)
                throw new InvalidOperationException(
                    $"LoadLibraryW reported success but '{moduleFileName}' could not be found in the target's module list.");
            return realHandle;
```

Then add a sibling helper just below `IsModuleLoaded`:

```csharp
public static IntPtr FindModuleHandle(int processId, string moduleFileName)
{
    var hProcess = NativeMethods.OpenProcess(
        NativeMethods.ProcessQueryInformation | NativeMethods.ProcessVmRead,
        false, processId);
    if (hProcess == IntPtr.Zero) return IntPtr.Zero;

    try
    {
        uint needed = 0;
        NativeMethods.EnumProcessModulesEx(hProcess, null!, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
        if (needed == 0) return IntPtr.Zero;

        var count = needed / (uint)IntPtr.Size;
        var modules = new IntPtr[count];
        if (!NativeMethods.EnumProcessModulesEx(hProcess, modules, needed, out needed, NativeMethods.LIST_MODULES_DEFAULT))
            return IntPtr.Zero;

        var sb = new System.Text.StringBuilder(260);
        foreach (var m in modules)
        {
            sb.Clear();
            NativeMethods.GetModuleFileNameExW(hProcess, m, sb, (uint)sb.Capacity);
            if (sb.Length == 0) continue;
            if (Path.GetFileName(sb.ToString()).Equals(moduleFileName, StringComparison.OrdinalIgnoreCase))
                return m;
        }
        return IntPtr.Zero;
    }
    finally
    {
        NativeMethods.CloseHandle(hProcess);
    }
}
```

### Step 3 — Improve the `WaitForSingleObject` failure message

In `InjectLoadLibrary`, line 51–53:

```csharp
var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
if (wait != 0)
    throw new TimeoutException("Remote LoadLibraryW did not complete in time.");
```

Replace with:

```csharp
var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
switch (wait)
{
    case 0: break;                                         // WAIT_OBJECT_0
    case 0x00000102: throw new TimeoutException("Remote LoadLibraryW did not complete in 30 seconds.");
    case 0xFFFFFFFF: throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitForSingleObject failed.");
    default: throw new InvalidOperationException($"Unexpected WaitForSingleObject result: 0x{wait:X8}.");
}
```

## Verification

1. **Build** — `dotnet build`.
2. **Inject + resolve** — inject the bot into the host, log the returned `IntPtr`, confirm:
   - It's non-zero.
   - Its lower 32 bits match `EnumProcessModulesEx`'s record.
   - Its upper 32 bits are non-zero (proving we're returning a full 64-bit handle, not the truncated thread-exit version).
3. **Negative test** — point `absoluteDllPath` at a DLL with missing dependencies; confirm the throw message is the new "could not be found in module list" line, not the old "returned NULL" one.

## Don't do

- Do not "fix" by changing the P/Invoke signature of `GetExitCodeThread` to `out long`. The OS only writes 4 bytes; the upper bytes are uninitialized stack memory.
- Do not skip the file-name match in `FindModuleHandle` and just return `modules[modules.Length - 1]`. Module load order isn't guaranteed; you might return a different recently-loaded module.
- Do not reuse the `hProcess` handle from the inject step in `FindModuleHandle` — it's been closed by the `finally` in `InjectLoadLibrary`. Open a fresh one.

## Related
- **P2/04** — same `EnumProcessModulesEx` call site has a separate `null` argument issue. Bundle.
