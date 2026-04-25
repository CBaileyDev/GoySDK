// P0/04: Authenticode signature verification + signer subject extraction.
//
// The loader downloads dependency installers (vc_redist.x64.exe, ViGEmBus_*.exe)
// and runs them with administrator privileges (per app.manifest). Without a
// signature gate, a MITM attacker, a compromised origin CDN, or a poisoned
// upstream release would get admin-context arbitrary code execution.
//
// Use AuthenticodeVerifier.Verify(filePath) to get an AuthenticodeResult that
// describes both the WinVerifyTrust outcome and (best-effort) the embedded
// signer's certificate subject. Callers MUST also enforce a publisher
// allow-list against the returned subject; signature validity alone only proves
// "signed by SOMEONE with a chain to a Microsoft trust root."

using System.ComponentModel;
using System.IO;
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

    /// <summary>
    /// Verify a file's embedded Authenticode signature. Returns Trusted=true only
    /// if WinVerifyTrust accepts the chain. Subject/Thumbprint are populated when
    /// the embedded certificate is readable (independent of WinVerifyTrust's
    /// success), so callers can log signer info even on failure.
    /// </summary>
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
#pragma warning disable SYSLIB0057 // CreateFromSignedFile is available; replacement API for embedded sig is Authenticode-specific.
                using var rawCert = X509Certificate.CreateFromSignedFile(filePath);
                using var cert = new X509Certificate2(rawCert);
#pragma warning restore SYSLIB0057
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
