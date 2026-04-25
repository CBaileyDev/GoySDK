# P0 / 04 — Downloaded installers run elevated with no signature check

## TL;DR
`DependencySetupService` downloads `vc_redist.x64.exe` (from `aka.ms`) and `ViGEmBus_*.exe` (from `github.com`) and immediately spawns them via `Process.Start` with no integrity verification — no Authenticode signature check, no SHA-256 pin, no certificate pinning on the TLS connection. The loader runs with `requireAdministrator`, so any binary that lands in the cache dir runs as admin.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DependencySetupService.cs`
- Functions: `DownloadAndInstallVcRedistAsync` (line 65), `DownloadAndInstallViGEmBusAsync` (line 105), `RunInstallerAsync` (line 203)
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`
- Function: `DownloadToFileCoreAsync` (line 59)

## Problem
The current pipeline:
1. `FileDownloader.DownloadToFileAsync` opens an HTTPS connection with default `HttpClientHandler` settings and writes the body to disk.
2. `RunInstallerAsync` calls `Process.Start` on the resulting file with `UseShellExecute = true`.
3. The loader is admin (per `app.manifest`), so the child inherits the elevated context.

There is **no point** between download and execute where the file's authenticity is verified. The threats covered:

| Threat | Mitigated today? |
|---|---|
| Plain HTTP downgrade | Mitigated by `HttpClient` defaulting to TLS for `https://` URLs |
| MITM with a CA-signed cert (e.g. malicious enterprise root) | **No** |
| Origin compromise (Microsoft CDN cache poisoning, GitHub release replacement) | **No** |
| DNS hijack to a different HTTPS endpoint that serves a valid cert for a different domain | Partially — TLS hostname check catches this |
| Malicious upstream release | **No** — see also P0/03 |

## Why it matters
This is the kind of supply-chain hole you read about in incident reports. The loader is exactly the wrong place to be casual about this — it's already admin, it's an attractive escalation target, and users explicitly trust it.

## Root cause
"`Process.Start(downloadedFile)` ships fast." The author treated MS and GitHub as inherently trustworthy origins. They are *usually* trustworthy, but a security boundary should not be "usually."

## Fix

There are two layers of defense to add: **Authenticode signature verification** (catches MITM and origin compromise for signed binaries) and **SHA-256 pinning** (catches both, but requires bumping the pin on each upstream release).

### Step 1 — Authenticode verification of every downloaded `.exe`

Add a new file `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/AuthenticodeVerifier.cs`:

```csharp
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace GoyLoader.Services;

public static class AuthenticodeVerifier
{
    private const uint WTD_UI_NONE = 2;
    private const uint WTD_REVOKE_NONE = 0;
    private const uint WTD_CHOICE_FILE = 1;
    private const uint WTD_STATEACTION_VERIFY = 1;
    private const uint WTD_STATEACTION_CLOSE = 2;
    private const uint WTD_SAFER_FLAG = 0x100;

    private static readonly Guid WINTRUST_ACTION_GENERIC_VERIFY_V2 =
        new("00AAC56B-CD44-11D0-8CC2-00C04FC295EE");

    [StructLayout(LayoutKind.Sequential)]
    private struct WINTRUST_FILE_INFO
    {
        public uint cbStruct;
        [MarshalAs(UnmanagedType.LPWStr)] public string pcwszFilePath;
        public IntPtr hFile;
        public IntPtr pgKnownSubject;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WINTRUST_DATA
    {
        public uint cbStruct;
        public IntPtr pPolicyCallbackData;
        public IntPtr pSIPClientData;
        public uint dwUIChoice;
        public uint fdwRevocationChecks;
        public uint dwUnionChoice;
        public IntPtr pFile;
        public uint dwStateAction;
        public IntPtr hWVTStateData;
        [MarshalAs(UnmanagedType.LPWStr)] public string? pwszURLReference;
        public uint dwProvFlags;
        public uint dwUIContext;
        public IntPtr pSignatureSettings;
    }

    [DllImport("wintrust.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int WinVerifyTrust(IntPtr hwnd, ref Guid pgActionID, ref WINTRUST_DATA pWVTData);

    /// <summary>Returns true only if the file has a valid embedded Authenticode signature.</summary>
    public static bool IsTrusted(string filePath, out string detail)
    {
        var fileInfo = new WINTRUST_FILE_INFO
        {
            cbStruct = (uint)Marshal.SizeOf<WINTRUST_FILE_INFO>(),
            pcwszFilePath = filePath,
            hFile = IntPtr.Zero,
            pgKnownSubject = IntPtr.Zero,
        };
        var pFile = Marshal.AllocHGlobal(Marshal.SizeOf(fileInfo));
        try
        {
            Marshal.StructureToPtr(fileInfo, pFile, false);
            var data = new WINTRUST_DATA
            {
                cbStruct = (uint)Marshal.SizeOf<WINTRUST_DATA>(),
                dwUIChoice = WTD_UI_NONE,
                fdwRevocationChecks = WTD_REVOKE_NONE,
                dwUnionChoice = WTD_CHOICE_FILE,
                pFile = pFile,
                dwStateAction = WTD_STATEACTION_VERIFY,
                dwProvFlags = WTD_SAFER_FLAG,
            };
            var actionId = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            var result = WinVerifyTrust(IntPtr.Zero, ref actionId, ref data);

            data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(IntPtr.Zero, ref actionId, ref data);

            if (result == 0)
            {
                detail = "Authenticode signature valid.";
                return true;
            }
            detail = $"WinVerifyTrust returned 0x{result:X8} ({new Win32Exception(result).Message}).";
            return false;
        }
        finally
        {
            Marshal.FreeHGlobal(pFile);
        }
    }
}
```

Optionally, also verify the signer subject. Use `System.Security.Cryptography.X509Certificates.X509Certificate.CreateFromSignedFile(filePath)` and check `cert.Subject` contains `"O=Microsoft Corporation"` (for VC++) or `"O=Nefarius Software Solutions"` (for ViGEmBus). This catches "valid signature but signed by attacker's own CA."

### Step 2 — Verify before executing

In `RunInstallerAsync` (`DependencySetupService.cs:203`), find:
```csharp
private static async Task RunInstallerAsync(string fileName, string arguments, bool treatAsVcRedist, CancellationToken cancellationToken)
{
    // Already running elevated (app manifest). No Verb=runas to avoid a second UAC prompt.
    var psi = new ProcessStartInfo
    {
        FileName = fileName,
```

Insert immediately at the top of the method body, before the `var psi = ...` line:
```csharp
    if (!AuthenticodeVerifier.IsTrusted(fileName, out var trustDetail))
        throw new InvalidOperationException(
            $"Refusing to execute installer with invalid Authenticode signature: {fileName}\n{trustDetail}");
```

### Step 3 — Pin the VC++ download SHA-256

The VC++ x64 redistributable URL `https://aka.ms/vs/17/release/vc_redist.x64.exe` is a stable redirect maintained by Microsoft. Pin its current SHA-256:

In `DependencyChecker.cs`, near `VcRedistDownloadUrl`, add:
```csharp
public const string VcRedistDownloadSha256 =
    // PIN: update on each VC++ runtime version bump. Verify against the hash
    // published by Microsoft at https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
    "TODO_PIN_BEFORE_SHIPPING";
```

In `DownloadAndInstallVcRedistAsync` (`DependencySetupService.cs:65`), after the `await FileDownloader.DownloadToFileAsync(...)` call (line 78), add:
```csharp
using (var stream = File.OpenRead(dest))
{
    var actual = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(stream)).ToLowerInvariant();
    var expected = DependencyChecker.VcRedistDownloadSha256.ToLowerInvariant();
    if (expected != "todo_pin_before_shipping" && actual != expected)
        throw new InvalidOperationException(
            $"VC++ installer SHA-256 mismatch.\n  expected: {expected}\n  actual:   {actual}\n" +
            "If Microsoft has shipped a newer runtime, update VcRedistDownloadSha256 after verifying the new hash.");
}
```

(The `expected != "todo_pin_before_shipping"` guard lets development builds compile while the maintainer pins the real hash.)

### Step 4 — Don't pin ViGEmBus by SHA — verify by signer

ViGEmBus releases change frequently. Use Authenticode signer-subject check instead. After Step 2, also assert in `DownloadAndInstallViGEmBusAsync` that the signed-by subject contains `"Nefarius"`:

```csharp
using (var cert = System.Security.Cryptography.X509Certificates.X509Certificate.CreateFromSignedFile(dest))
{
    if (!cert.Subject.Contains("Nefarius", StringComparison.OrdinalIgnoreCase))
        throw new InvalidOperationException(
            $"ViGEmBus installer is signed by an unexpected publisher: {cert.Subject}");
}
```

Place this after the Authenticode check, before `await RunInstallerAsync(...)`.

### Step 5 — Tighten TLS

In `FileDownloader.DownloadToFileCoreAsync` (`FileDownloader.cs:69`), find:
```csharp
using var handler = new HttpClientHandler
{
    AllowAutoRedirect = true,
    AutomaticDecompression = DecompressionMethods.All
};
```

Replace with:
```csharp
using var handler = new HttpClientHandler
{
    AllowAutoRedirect = true,
    AutomaticDecompression = DecompressionMethods.All,
    SslProtocols = System.Security.Authentication.SslProtocols.Tls12 | System.Security.Authentication.SslProtocols.Tls13,
    CheckCertificateRevocationList = true,
};
```

This forces TLS 1.2+ and turns on CRL checking so a revoked cert can't be used.

## Verification

1. **Build** — `dotnet build /Users/carterbarker/Documents/GoySDK/GoyLoader/GoyLoader.csproj`.
2. **Authenticode positive** — call `AuthenticodeVerifier.IsTrusted(@"C:\Windows\System32\notepad.exe", out var d)` and assert it returns `true` with a Microsoft signer.
3. **Authenticode negative** — generate any unsigned `.exe` (e.g. compile a hello-world without `signtool`) and assert `IsTrusted` returns `false`.
4. **End-to-end** — point `VcRedistDownloadUrl` at a local web server that serves a binary you tampered with by appending one byte; confirm the SHA pin trips and the loader refuses to install.
5. **Cert revocation** — there's no easy local test for this; verify the production behavior by intentionally letting the cert expire on a test deployment.

## Don't do

- Do not call `Process.Start` *before* `AuthenticodeVerifier.IsTrusted`. The whole point is to fail closed.
- Do not skip Step 5 — TLS without CRL checking can't tell that an attacker's stolen cert has been revoked.
- Do not pin SHA for the ViGEmBus installer — releases happen often and a stale pin will block legitimate updates. Authenticode + signer subject is the right control there.
- Do not catch and swallow the new `InvalidOperationException`s in upstream callers. The user-facing message produced by these throws is the security signal.

## Related
- **P0/03** — fixes the path-traversal write sink. This fix prevents executing a tampered file even if it lands in the right directory.
