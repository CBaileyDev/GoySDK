# P3 / 05 — `ProcessFinder` XOR obfuscation gives false confidence

## TL;DR
`ProcessFinder` XOR-encodes the host process name, Steam library subdir, and exe name with a constant key (`0x5A`). It then decodes them at runtime via `Dec()` which returns a managed `string`. The encoded byte arrays AND the decoded strings live in process memory simultaneously; the decode is also visible in IL. This stops `strings(1)` and not much else. Document the limitation, add basic friction, or remove it.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/ProcessFinder.cs`
- Constants and decoder: lines **9–27**

## Problem
1. `Dec(EncProcName)` returns `"RocketLeague"` after one XOR pass; decoding the bytes is trivial:
   ```python
   bytes_to_xor = [0x08,0x35,0x39,0x31,0x3F,0x2E,0x16,0x3F,0x3B,0x3D,0x2F,0x3F]
   "".join(chr(b ^ 0x5A) for b in bytes_to_xor)  # "RocketLeague"
   ```
2. Once `Dec()` runs, the result is in the .NET string intern pool. A memory dump of the loader contains both the encoded array and the decoded string.
3. The function names `Dec`, `EncProcName`, `EncHostExe` are themselves giveaways to anyone reading the disassembly.

The mistake isn't *having* obfuscation; the mistake is calling it "reduces detection" when in practice it offers no security benefit at all.

## Why it matters
False security. If anyone is making decisions based on "we obfuscate the process name," they're wrong. Either commit to real anti-analysis or be honest that this is `strings(1)` cosmetic.

## Fix

Two acceptable resolutions: tighten and document, or remove.

### Option A — Document the actual threat model (recommended for a research SDK)

Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/ProcessFinder.cs`. Find the comment at line 11:

```csharp
    // Encoded with XOR K — runtime only (no full plaintext in IL string heap for naive grep).
```

Replace with:

```csharp
    // Encoded with XOR K. This defeats `strings(1)` and naive grep ONLY.
    // It does NOT hide the strings from a memory dump, IL inspector, or any
    // dynamic analysis. If a real threat model demands string hiding, replace
    // with a proper packer/cipher and a wipe-after-use buffer; do not extend this.
```

That single comment change makes the security posture honest. Any future maintainer reading it will know not to lean on this.

### Option B — Remove the obfuscation entirely

If the project doesn't actually need to hide from `strings(1)` (most legitimate research projects don't), delete the encoded arrays and the `Dec()` helper, and inline the strings:

Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/ProcessFinder.cs`. Replace the entire body of `ProcessFinder` with:

```csharp
public static class ProcessFinder
{
    private const string ProcessName = "RocketLeague";
    private const string SteamSubdir = "rocketleague";
    private const string HostExe     = "RocketLeague.exe";

    public static IReadOnlyList<Process> FindHostProcesses()
    {
        try { return Process.GetProcessesByName(ProcessName); }
        catch { return Array.Empty<Process>(); }
    }

    public static string? TryGetSteamInstalledExe()
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            var steamPath = k?.GetValue("SteamPath") as string;
            if (string.IsNullOrEmpty(steamPath)) return null;
            steamPath = steamPath.Replace('/', '\\');
            var candidate = Path.Combine(steamPath, "steamapps", "common", SteamSubdir, "Binaries", "Win64", HostExe);
            return File.Exists(candidate) ? candidate : null;
        }
        catch { return null; }
    }

    public static bool TryLaunchSteamGame()
    {
        var exe = TryGetSteamInstalledExe();
        if (exe == null) return false;
        try
        {
            Process.Start(new ProcessStartInfo { FileName = exe, UseShellExecute = true });
            return true;
        }
        catch { return false; }
    }
}
```

(Verify the actual values of the encoded strings first by running the XOR mentally or with a one-liner; the values above are derived from the published bytes.)

### Option C — If you genuinely need obfuscation, do it right

Outside the scope of a code-review fix, but for completeness: use AES with a key derived from a system-bound source (machine GUID), wipe the decoded buffer after use (`SecureString` or pinned `byte[]` with manual zeroing), and ensure the decoded string never enters the .NET intern pool. This is non-trivial; only do it if there's a real adversary model.

## Verification

### If Option A
- Build. Confirm no behavior change.
- Read the new comment yourself and decide if it accurately describes your threat model.

### If Option B
- Build. Inject. Confirm the loader still finds the host process and the Steam install path.
- Run `strings GoyLoader.exe | grep -i rocket` — pre-fix would not match; post-fix will. Confirm that's acceptable for your threat model before shipping.

## Don't do
- Don't add a second XOR pass with a different key. That's still trivially defeated and signals "tried harder, failed harder."
- Don't compute the strings dynamically from registry/file paths to avoid embedding them. The strings will still appear in memory at runtime — same problem.
