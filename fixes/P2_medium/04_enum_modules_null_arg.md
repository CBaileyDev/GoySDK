# P2 / 04 — `EnumProcessModulesEx` first call passes `null` to `[Out]` array

## TL;DR
`DllInjector.IsModuleLoaded` calls `EnumProcessModulesEx(hProcess, null, 0, out needed, ...)` to discover the required buffer size. The P/Invoke signature declares `lphModule` as `[Out] IntPtr[] lphModule` (non-nullable). Passing `null` to a non-nullable `[Out]` array is implementation-defined under the marshaling layer; current .NET tolerates it but it's UB-adjacent and Roslyn's nullable analysis flags it.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`
- Function: `IsModuleLoaded`
- Lines: **78–82** (and again in the new `FindModuleHandle` from P2/03)

```csharp
uint needed = 0;
// first call to get needed size
NativeMethods.EnumProcessModulesEx(hProcess, null, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
if (needed == 0)
    return false;
```

P/Invoke declaration:
```csharp
[DllImport("psapi.dll", SetLastError = true)]
public static extern bool EnumProcessModulesEx(IntPtr hProcess, [Out] IntPtr[] lphModule, uint cb, out uint lpcbNeeded, uint dwFilterFlag);
```

## Problem
The marshaler doesn't have a defined contract for "non-nullable `[Out]` array, but caller passes `null`." Today it works because the marshaler sees `cb == 0` and elides the marshaled buffer. There's no guarantee future runtimes will keep that behavior.

It's also a `null!` lie when the calling code uses nullable-reference-types (`<Nullable>enable</Nullable>`).

## Why it matters
Tomorrow's .NET upgrade could turn this into a `NullReferenceException` at runtime. Cheap to fix today.

## Root cause
The Win32 API's "size discovery" pattern (call once with NULL to get size, call again with allocated buffer) doesn't translate cleanly to the .NET array marshaler. The fix is to declare `lphModule` as nullable.

## Fix

### Step 1 — Mark the P/Invoke parameter nullable

Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Native/NativeMethods.cs`. Find:

```csharp
[DllImport("psapi.dll", SetLastError = true)]
public static extern bool EnumProcessModulesEx(IntPtr hProcess, [Out] IntPtr[] lphModule, uint cb, out uint lpcbNeeded, uint dwFilterFlag);
```

Replace with:

```csharp
[DllImport("psapi.dll", SetLastError = true)]
public static extern bool EnumProcessModulesEx(IntPtr hProcess, [Out] IntPtr[]? lphModule, uint cb, out uint lpcbNeeded, uint dwFilterFlag);
```

(Note the `?` after `IntPtr[]`.)

### Step 2 — Update call sites

Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DllInjector.cs`. Find (in `IsModuleLoaded`, line 80):

```csharp
NativeMethods.EnumProcessModulesEx(hProcess, null, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
```

No code change needed — the parameter is now nullable, so passing `null` is no longer a warning. But for clarity, you can replace `null` with the explicit cast:

```csharp
NativeMethods.EnumProcessModulesEx(hProcess, (IntPtr[]?)null, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
```

If `FindModuleHandle` was added per **P2/03**, apply the same change there.

### Step 3 — (Optional) Wrap the size-discovery pattern in a helper

If the size-then-fetch pattern repeats in more than two places, extract it:

```csharp
private static IntPtr[]? EnumModulesAll(IntPtr hProcess, uint filter)
{
    uint needed = 0;
    NativeMethods.EnumProcessModulesEx(hProcess, (IntPtr[]?)null, 0, out needed, filter);
    if (needed == 0) return null;
    var count = needed / (uint)IntPtr.Size;
    var modules = new IntPtr[count];
    if (!NativeMethods.EnumProcessModulesEx(hProcess, modules, needed, out needed, filter))
        return null;
    return modules;
}
```

Then `IsModuleLoaded` and `FindModuleHandle` collapse to a single foreach.

## Verification

1. **Build** — `dotnet build`. No nullable-reference warnings on the P/Invoke parameter.
2. **Functional test** — call `IsModuleLoaded(targetPid, "kernel32.dll")` and assert it returns `true` (every Windows process has kernel32 loaded).
3. **Regression** — call `IsModuleLoaded(targetPid, "definitely_not_loaded_xyz.dll")` and assert it returns `false` without throwing.

## Don't do

- Do not change the P/Invoke parameter to `IntPtr` (single pointer) and use `Marshal.AllocHGlobal`. The current array-marshaled signature is fine; just allow null.
- Do not check `needed > 0` before calling — you can't know the size without calling first. The pattern is correct; only the nullability annotation needs updating.
- Do not catch `NullReferenceException` "just in case." The fix is to make the call legal, not to paper over it.

## Related
- **P2/03** — same file, same `EnumProcessModulesEx` API, do together.
