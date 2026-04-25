using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace GoyLoader.Services;

public static class ProcessFinder
{
    private const byte K = 0x5A;

    // P3/05: encoded with XOR K. This defeats `strings(1)` and naive grep ONLY.
    // It does NOT hide the strings from a memory dump, IL inspector, or any
    // dynamic analysis. If a real threat model demands string hiding, replace
    // with a proper packer/cipher and a wipe-after-use buffer; do not extend this.
    private static ReadOnlySpan<byte> EncProcName => // 12 bytes
        [0x08, 0x35, 0x39, 0x31, 0x3F, 0x2E, 0x16, 0x3F, 0x3B, 0x3D, 0x2F, 0x3F];

    private static ReadOnlySpan<byte> EncSteamSubdir => // 13 bytes (library folder)
        [0x28, 0x35, 0x39, 0x31, 0x3F, 0x2E, 0x7A, 0x36, 0x3F, 0x3B, 0x3D, 0x2F, 0x3F];

    private static ReadOnlySpan<byte> EncHostExe => // 16 bytes
        [0x08, 0x35, 0x39, 0x31, 0x3F, 0x2E, 0x16, 0x3F, 0x3B, 0x3D, 0x2F, 0x3F, 0x74, 0x3F, 0x22, 0x3F];

    private static string Dec(ReadOnlySpan<byte> enc)
    {
        var b = new char[enc.Length];
        for (var i = 0; i < enc.Length; i++)
            b[i] = (char)(enc[i] ^ K);
        return new string(b);
    }

    public static IReadOnlyList<Process> FindHostProcesses()
    {
        try
        {
            return Process.GetProcessesByName(Dec(EncProcName));
        }
        catch
        {
            return Array.Empty<Process>();
        }
    }

    public static string? TryGetSteamInstalledExe()
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            var steamPath = k?.GetValue("SteamPath") as string;
            if (string.IsNullOrEmpty(steamPath))
                return null;
            steamPath = steamPath.Replace('/', '\\');
            var candidate = Path.Combine(steamPath, "steamapps", "common", Dec(EncSteamSubdir), "Binaries", "Win64", Dec(EncHostExe));
            return File.Exists(candidate) ? candidate : null;
        }
        catch
        {
            return null;
        }
    }

    public static bool TryLaunchSteamGame()
    {
        var exe = TryGetSteamInstalledExe();
        if (exe == null)
            return false;
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = exe,
                UseShellExecute = true
            });
            return true;
        }
        catch
        {
            return false;
        }
    }
}
