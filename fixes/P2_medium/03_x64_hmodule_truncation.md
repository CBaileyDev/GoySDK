# P2 / 03 — `InjectLoadLibrary` returns a truncated `HMODULE` on x64

## TL;DR
`DllInjector.InjectLoadLibrary` uses `GetExitCodeThread` to read the return value of a remote `LoadLibraryW` call and returns that value as an `IntPtr`. On Windows, thread exit codes are `DWORD` values. On x64, `HMODULE` is pointer-sized, so the thread exit channel can only reliably tell us “non-zero success,” not the full module handle.

The current caller discards the return value, so the bug is latent. But if a future unload feature passes this truncated handle to `FreeLibrary`, it can fail or crash the target process. Fix by treating `GetExitCodeThread` as a 32-bit success signal and resolving the real module handle with `EnumProcessModulesEx`.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`
- Function: `InjectLoadLibrary`, current lines around 47-58.
- P/Invoke signature: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Native/NativeMethods.cs`, `GetExitCodeThread`, current line around 41.
- Module enumeration: `DllInjector.IsModuleLoaded`, current lines around 70-106.

## Correct Fix Strategy
1. Change `GetExitCodeThread` P/Invoke to `out uint`, matching the Win32 `LPDWORD`.
2. Use the exit code only as `0 == LoadLibrary failed`, `non-zero == likely success`.
3. After the remote thread completes, enumerate target modules and match the loaded DLL by full path or filename.
4. Return the handle from `EnumProcessModulesEx`, not the thread exit code.
5. Pair this with **P2/04** so the nullable first `EnumProcessModulesEx` call is correctly declared.

## Step 1 — Fix `GetExitCodeThread` Signature
Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Native/NativeMethods.cs`.

Replace:

```csharp
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool GetExitCodeThread(IntPtr hThread, out IntPtr lpExitCode);
```

with:

```csharp
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);
```

This makes the truncation explicit instead of hiding it inside an `IntPtr`.

## Step 2 — Add Wait Result Constants
In `NativeMethods.cs`, near `Infinite`, add:

```csharp
public const uint WaitObject0 = 0x00000000;
public const uint WaitTimeout = 0x00000102;
public const uint WaitFailed = 0xFFFFFFFF;
```

This makes `WaitForSingleObject` result handling readable.

## Step 3 — Rename or Add Module Enumeration Constant
Current code defines:

```csharp
public const uint LIST_MODULES_DEFAULT = 0x03; // LIST_MODULES_32BIT | LIST_MODULES_64BIT
```

`0x03` is effectively “all modules” for `EnumProcessModulesEx`; the name is misleading. Replace with:

```csharp
public const uint LIST_MODULES_ALL = 0x03;
```

Then update all call sites from `LIST_MODULES_DEFAULT` to `LIST_MODULES_ALL`.

## Step 4 — Add `FindModuleHandle`
Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`.

Add this helper below `IsModuleLoaded` or replace `IsModuleLoaded` with shared enumeration helpers:

```csharp
public static IntPtr FindModuleHandle(int processId, string expectedDllPathOrName)
{
    var expectedFullPath = Path.IsPathRooted(expectedDllPathOrName)
        ? Path.GetFullPath(expectedDllPathOrName)
        : null;
    var expectedFileName = Path.GetFileName(expectedDllPathOrName);
    if (string.IsNullOrWhiteSpace(expectedFileName))
        return IntPtr.Zero;

    var hProcess = NativeMethods.OpenProcess(
        NativeMethods.ProcessQueryInformation | NativeMethods.ProcessVmRead,
        false,
        processId);
    if (hProcess == IntPtr.Zero)
        return IntPtr.Zero;

    try
    {
        uint needed = 0;
        NativeMethods.EnumProcessModulesEx(
            hProcess,
            (IntPtr[]?)null,
            0,
            out needed,
            NativeMethods.LIST_MODULES_ALL);

        if (needed == 0)
            return IntPtr.Zero;

        var count = needed / (uint)IntPtr.Size;
        var modules = new IntPtr[count];
        if (!NativeMethods.EnumProcessModulesEx(
                hProcess,
                modules,
                needed,
                out needed,
                NativeMethods.LIST_MODULES_ALL))
        {
            return IntPtr.Zero;
        }

        var sb = new System.Text.StringBuilder(1024);
        foreach (var module in modules)
        {
            sb.Clear();
            NativeMethods.GetModuleFileNameExW(hProcess, module, sb, (uint)sb.Capacity);
            if (sb.Length == 0)
                continue;

            var modulePath = sb.ToString();
            if (expectedFullPath != null)
            {
                try
                {
                    if (string.Equals(
                            Path.GetFullPath(modulePath),
                            expectedFullPath,
                            StringComparison.OrdinalIgnoreCase))
                    {
                        return module;
                    }
                }
                catch
                {
                    // Fall back to file-name match below.
                }
            }

            if (string.Equals(
                    Path.GetFileName(modulePath),
                    expectedFileName,
                    StringComparison.OrdinalIgnoreCase))
            {
                return module;
            }
        }

        return IntPtr.Zero;
    }
    finally
    {
        NativeMethods.CloseHandle(hProcess);
    }
}
```

This uses full-path matching when possible and filename matching as a fallback.

## Step 5 — Update `InjectLoadLibrary`
In `InjectLoadLibrary`, replace:

```csharp
var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
if (wait != 0)
    throw new TimeoutException("Remote LoadLibraryW did not complete in time.");

if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode) || exitCode == IntPtr.Zero)
    throw new InvalidOperationException("LoadLibraryW returned NULL in the target process (missing dependencies or wrong architecture).");

return exitCode;
```

with:

```csharp
var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
switch (wait)
{
    case NativeMethods.WaitObject0:
        break;
    case NativeMethods.WaitTimeout:
        throw new TimeoutException("Remote LoadLibraryW did not complete in 30 seconds.");
    case NativeMethods.WaitFailed:
        throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitForSingleObject failed.");
    default:
        throw new InvalidOperationException($"Unexpected WaitForSingleObject result: 0x{wait:X8}.");
}

if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode))
    throw new Win32Exception(Marshal.GetLastWin32Error(), "GetExitCodeThread failed.");

if (exitCode == 0)
    throw new InvalidOperationException("LoadLibraryW returned NULL in the target process (missing dependencies or wrong architecture).");

var realHandle = FindModuleHandle(processId, absoluteDllPath);
if (realHandle == IntPtr.Zero)
{
    throw new InvalidOperationException(
        $"LoadLibraryW reported success, but '{Path.GetFileName(absoluteDllPath)}' was not found in the target module list.");
}

return realHandle;
```

Do not return `new IntPtr(exitCode)`. That is the old truncation bug made explicit.

## Step 6 — Update `IsModuleLoaded` to Reuse the Helper
Optional but recommended:

```csharp
public static bool IsModuleLoaded(int processId, string moduleFileName)
{
    return FindModuleHandle(processId, moduleFileName) != IntPtr.Zero;
}
```

If you keep the existing implementation, still apply **P2/04** to make the first `EnumProcessModulesEx` call nullable and rename the module-list constant.

## Step 7 — Consider Duplicate Module Names
Filename fallback can be ambiguous if two modules with the same filename are loaded from different paths. That is unlikely for the bot DLL but possible in general.

For injection, always call:

```csharp
FindModuleHandle(processId, absoluteDllPath)
```

not:

```csharp
FindModuleHandle(processId, Path.GetFileName(absoluteDllPath))
```

The helper can use full-path comparison first and filename fallback only when full-path normalization fails.

## Verification
1. Build:
   ```bash
   dotnet build /Users/carterbarker/Documents/GoySDK/GoyLoader/GoyLoader.csproj
   ```

2. Static check:
   ```bash
   rg -n "GetExitCodeThread|LIST_MODULES_DEFAULT|LIST_MODULES_ALL|FindModuleHandle" /Users/carterbarker/Documents/GoySDK/GoyLoader
   ```
   Expected:
   - `GetExitCodeThread` uses `out uint`.
   - No `LIST_MODULES_DEFAULT`.
   - `FindModuleHandle` exists.

3. Injection test:
   - Inject normally.
   - Log the returned `IntPtr`.
   - Confirm it matches the module handle returned by `EnumProcessModulesEx`.

4. x64 proof:
   - On a 64-bit target, confirm the returned handle can have non-zero upper 32 bits.
   - Confirm the old `exitCode` is only 32-bit and is not returned.

5. Negative dependency test:
   - Point `absoluteDllPath` at a DLL with missing dependencies or wrong architecture.
   - Confirm the failure message remains clear and no bogus module handle is returned.

## Don't Do
- Do not change `GetExitCodeThread` to `out long` or `out IntPtr`. The OS API writes a 32-bit `DWORD`.
- Do not return the thread exit code as the module handle on x64.
- Do not return the last module in the list. Module load order is not a contract.
- Do not compare only by filename when you have the absolute DLL path.

## Related
- **P2/04** — required nullability cleanup for `EnumProcessModulesEx`.
