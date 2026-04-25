# P0 / 04 — Downloaded installers run elevated with no signature verification

## TL;DR
`DependencySetupService` downloads dependency installers and immediately runs them from an elevated process:

- Microsoft VC++ redistributable from `https://aka.ms/vs/17/release/vc_redist.x64.exe`.
- ViGEmBus installer from the GitHub releases API or a hardcoded GitHub fallback URL.

The current code verifies neither the file's Authenticode signature nor the signer identity before `Process.Start`. Because `GoyLoader/app.manifest` requests administrator privileges, any binary that lands at the destination path executes elevated.

Fix this by failing closed unless the downloaded `.exe` has a valid Authenticode signature and the signer is one of the exact expected publishers. Optional SHA-256 pins may be added afterward, but they must not be implemented as a `TODO` bypass.

## Where
- Download VC++: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DependencySetupService.cs`, `DownloadAndInstallVcRedistAsync`, current lines around 65-81.
- Download ViGEmBus: same file, `DownloadAndInstallViGEmBusAsync`, current lines around 105-120.
- Execute installer: same file, `RunInstallerAsync`, current lines around 203-231.
- Downloader TLS settings: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`, current lines around 69-77.

## Correct Fix Strategy
Add a single verification gate immediately before any downloaded installer is executed:

1. Validate the file exists and has a `.exe` extension.
2. Use `WinVerifyTrust` to verify the embedded Authenticode signature.
3. Extract the signing certificate with `X509Certificate.CreateFromSignedFile`.
4. Compare the signer against an explicit allow-list for the installer kind.
5. Only then call `Process.Start`.

This should be a hard failure. Do not show a warning and continue.

## Step 1 — Add Installer Identity Enum
Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/DependencySetupService.cs`.

Add this private enum inside `DependencySetupService`:

```csharp
private enum InstallerKind
{
    VcRedist,
    ViGEmBus,
}
```

Change `RunInstallerAsync` from:

```csharp
private static async Task RunInstallerAsync(string fileName, string arguments, bool treatAsVcRedist, CancellationToken cancellationToken)
```

to:

```csharp
private static async Task RunInstallerAsync(
    string fileName,
    string arguments,
    InstallerKind installerKind,
    CancellationToken cancellationToken)
```

Update callers:

```csharp
await RunInstallerAsync(dest, "/install /quiet /norestart", InstallerKind.VcRedist, cancellationToken);
await RunInstallerAsync(dest, "/quiet /norestart", InstallerKind.ViGEmBus, cancellationToken);
```

Inside `RunInstallerAsync`, replace `treatAsVcRedist` checks with `installerKind == InstallerKind.VcRedist`.

## Step 2 — Add `AuthenticodeVerifier.cs`
Create `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/AuthenticodeVerifier.cs`:

```csharp
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

namespace GoyLoader.Services;

public sealed record AuthenticodeResult(
    bool Trusted,
    string Detail,
    string? Subject,
    string? Thumbprint);

public static class AuthenticodeVerifier
{
    private const uint WTD_UI_NONE = 2;
    private const uint WTD_REVOKE_WHOLECHAIN = 1;
    private const uint WTD_CHOICE_FILE = 1;
    private const uint WTD_STATEACTION_VERIFY = 1;
    private const uint WTD_STATEACTION_CLOSE = 2;

    private static readonly Guid WintrustActionGenericVerifyV2 =
        new("00AAC56B-CD44-11D0-8CC2-00C04FC295EE");

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WINTRUST_FILE_INFO
    {
        public uint cbStruct;
        [MarshalAs(UnmanagedType.LPWStr)] public string pcwszFilePath;
        public IntPtr hFile;
        public IntPtr pgKnownSubject;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
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
    private static extern int WinVerifyTrust(
        IntPtr hwnd,
        ref Guid pgActionID,
        ref WINTRUST_DATA pWVTData);

    public static AuthenticodeResult Verify(string filePath)
    {
        if (!File.Exists(filePath))
            return new(false, $"File does not exist: {filePath}", null, null);

        if (!filePath.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            return new(false, $"Not an .exe file: {filePath}", null, null);

        var fileInfo = new WINTRUST_FILE_INFO
        {
            cbStruct = (uint)Marshal.SizeOf<WINTRUST_FILE_INFO>(),
            pcwszFilePath = filePath,
            hFile = IntPtr.Zero,
            pgKnownSubject = IntPtr.Zero,
        };

        var pFile = Marshal.AllocHGlobal(Marshal.SizeOf<WINTRUST_FILE_INFO>());
        WINTRUST_DATA data = default;

        try
        {
            Marshal.StructureToPtr(fileInfo, pFile, false);
            data = new WINTRUST_DATA
            {
                cbStruct = (uint)Marshal.SizeOf<WINTRUST_DATA>(),
                dwUIChoice = WTD_UI_NONE,
                fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN,
                dwUnionChoice = WTD_CHOICE_FILE,
                pFile = pFile,
                dwStateAction = WTD_STATEACTION_VERIFY,
            };

            var actionId = WintrustActionGenericVerifyV2;
            var result = WinVerifyTrust(IntPtr.Zero, ref actionId, ref data);

            string? subject = null;
            string? thumbprint = null;
            try
            {
                using var cert = new X509Certificate2(X509Certificate.CreateFromSignedFile(filePath));
                subject = cert.Subject;
                thumbprint = cert.Thumbprint;
            }
            catch
            {
                // No certificate subject available. WinVerifyTrust result below is authoritative.
            }

            if (result == 0)
                return new(true, "Authenticode signature valid.", subject, thumbprint);

            return new(
                false,
                $"WinVerifyTrust returned 0x{result:X8} ({new Win32Exception(result).Message}).",
                subject,
                thumbprint);
        }
        finally
        {
            if (data.cbStruct != 0)
            {
                data.dwStateAction = WTD_STATEACTION_CLOSE;
                var actionId = WintrustActionGenericVerifyV2;
                WinVerifyTrust(IntPtr.Zero, ref actionId, ref data);
            }

            Marshal.FreeHGlobal(pFile);
        }
    }
}
```

Notes:
- This uses `WinVerifyTrust` for the trust decision.
- `X509Certificate2` is only used to display and allow-list the signer.
- Revocation checking is enabled with `WTD_REVOKE_WHOLECHAIN`. If this causes unacceptable offline failures, make that an explicit product decision. Do not silently disable revocation in a security fix.

## Step 3 — Add Exact Publisher Allow-List
In `DependencySetupService.cs`, add this helper inside the class:

```csharp
private static bool IsExpectedInstallerSigner(
    InstallerKind kind,
    string? subject,
    out string expectedDescription)
{
    // Keep this allow-list exact enough to prevent "valid signature by someone else"
    // from passing. If a publisher rotates certificate subjects, inspect the new
    // signed installer manually and update this list in the same commit.
    var allowedSubjectFragments = kind switch
    {
        InstallerKind.VcRedist => new[]
        {
            "O=Microsoft Corporation",
            "CN=Microsoft Corporation",
        },
        InstallerKind.ViGEmBus => new[]
        {
            "Nefarius",
            "Nefarius Software Solutions",
        },
        _ => Array.Empty<string>(),
    };

    expectedDescription = string.Join(" or ", allowedSubjectFragments);
    if (string.IsNullOrWhiteSpace(subject))
        return false;

    return allowedSubjectFragments.Any(fragment =>
        subject.Contains(fragment, StringComparison.OrdinalIgnoreCase));
}
```

Before shipping, run the verification below and replace the broad ViGEm fragments with the exact observed publisher string if possible. The current fragment-based version is still much safer than accepting any valid signature, but exact subject strings or thumbprints are better.

## Step 4 — Gate Execution in `RunInstallerAsync`
At the top of `RunInstallerAsync`, before creating `ProcessStartInfo`, add:

```csharp
var trust = AuthenticodeVerifier.Verify(fileName);
if (!trust.Trusted)
{
    throw new InvalidOperationException(
        $"Refusing to execute installer because Authenticode verification failed: {fileName}\n{trust.Detail}");
}

if (!IsExpectedInstallerSigner(installerKind, trust.Subject, out var expectedSigner))
{
    throw new InvalidOperationException(
        $"Refusing to execute installer signed by unexpected publisher.\n" +
        $"  file:     {fileName}\n" +
        $"  subject:  {trust.Subject ?? "<none>"}\n" +
        $"  expected: {expectedSigner}\n" +
        $"  thumb:    {trust.Thumbprint ?? "<none>"}");
}
```

Then keep the existing `Process.Start` logic.

After changing `bool treatAsVcRedist` to `InstallerKind installerKind`, update exit-code checks:

```csharp
if (installerKind == InstallerKind.VcRedist)
{
    if (!IsVcRedistSuccess(code))
        throw new InvalidOperationException($"VC++ installer exited with code {code}.");
}
else
{
    if (!IsInstallerProbablyOk(code) && code != 1638)
        throw new InvalidOperationException($"ViGEmBus installer exited with code {code}. Reboot if Windows requires it, then run this loader again.");
}
```

## Step 5 — Optional SHA-256 Pins, Fail-Closed Only
SHA pins are optional and should be used only if the project owner is prepared to update them when Microsoft or ViGEmBus ships a new release.

Do **not** add this kind of bypass:

```csharp
if (expected != "TODO_PIN_BEFORE_SHIPPING" && actual != expected) ...
```

That compiles and ships with no hash enforcement.

If you add pins, use a map that fails closed when a pin is configured:

```csharp
private static readonly IReadOnlyDictionary<InstallerKind, string> InstallerSha256Pins =
    new Dictionary<InstallerKind, string>
    {
        // Fill only with verified production hashes.
        // [InstallerKind.VcRedist] = "lowercasehex...",
    };

private static void VerifySha256IfPinned(InstallerKind kind, string fileName)
{
    if (!InstallerSha256Pins.TryGetValue(kind, out var expected))
        return;

    using var stream = File.OpenRead(fileName);
    var actual = Convert.ToHexString(
        System.Security.Cryptography.SHA256.HashData(stream)).ToLowerInvariant();

    if (!string.Equals(actual, expected, StringComparison.OrdinalIgnoreCase))
    {
        throw new InvalidOperationException(
            $"Installer SHA-256 mismatch for {kind}.\n  expected: {expected}\n  actual:   {actual}");
    }
}
```

Call it after Authenticode verification and before `Process.Start`:

```csharp
VerifySha256IfPinned(installerKind, fileName);
```

For the current project, Authenticode plus signer allow-list is the minimum required fix. SHA pinning is a release-policy choice.

## Step 6 — Tighten Downloader TLS Settings
Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`.

Current handler:

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
    CheckCertificateRevocationList = true,
    SslProtocols =
        System.Security.Authentication.SslProtocols.Tls12 |
        System.Security.Authentication.SslProtocols.Tls13,
};
```

Do the same for the GitHub API `HttpClientHandler` inside `DependencySetupService.GetLatestViGEmBusAssetAsync`.

This does not replace Authenticode verification. TLS protects transit; Authenticode verifies the executable.

## Step 7 — Pair With P0/03
Apply **P0/03** first or in the same commit. The execution gate does not replace path sanitization. You still must:

- Sanitize `asset.Name`.
- Resolve `dest` with `Path.GetFullPath`.
- Ensure `dest` stays inside `CacheDir`.
- Pin the asset download host to expected GitHub hosts.

## Verification
1. Build:
   ```bash
   dotnet build /Users/carterbarker/Documents/GoySDK/GoyLoader/GoyLoader.csproj
   ```

2. Positive Authenticode smoke test:
   - Temporarily call `AuthenticodeVerifier.Verify(@"C:\Windows\System32\notepad.exe")`.
   - Confirm `Trusted == true` and the subject is Microsoft.

3. Negative unsigned test:
   - Build any unsigned local `.exe`.
   - Confirm `AuthenticodeVerifier.Verify(path).Trusted == false`.

4. VC++ download test:
   - Trigger `DownloadAndInstallVcRedistAsync`.
   - Before execution, log `trust.Subject` once.
   - Confirm it contains an expected Microsoft publisher.

5. ViGEmBus download test:
   - Trigger `DownloadAndInstallViGEmBusAsync`.
   - Before execution, log `trust.Subject` once.
   - Tighten the allow-list to the observed legitimate publisher string if possible.

6. Tampered-file test:
   - Download a real signed installer.
   - Append one byte to the file.
   - Confirm Authenticode verification fails and `Process.Start` is never called.

7. Unexpected-signer test:
   - Use a validly signed `.exe` from a different publisher.
   - Confirm Authenticode passes but signer allow-list fails.

## Don't Do
- Do not run `Process.Start` before verification.
- Do not accept “any valid Authenticode signature.” A malicious or unrelated signed binary would pass.
- Do not ship a `TODO_PIN_BEFORE_SHIPPING` hash bypass.
- Do not disable revocation checks casually. If offline installs must work, document that tradeoff explicitly.
- Do not catch and swallow verification exceptions in `EnsurePrerequisitesAsync` for VC++. VC++ is required for injection and should fail visibly. ViGEmBus is optional and may keep the current “use Internal input” fallback.

## Related
- **P0/03** — path traversal and host validation for the GitHub asset.
- **P2/05** — retry/stall behavior in the same downloader.
