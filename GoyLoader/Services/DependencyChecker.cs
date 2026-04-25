using Microsoft.Win32;
using System.Management;
using System.ServiceProcess;

namespace GoyLoader.Services;

public sealed record DependencyReport(
    bool VcRedistX64Ok,
    string VcRedistDetail,
    ViGEmBusStatus ViGEm,
    string ViGEmDetail,
    NvidiaGpuStatus NvidiaGpu,
    string NvidiaGpuDetail);

public enum ViGEmBusStatus
{
    Running,
    Stopped,
    NotInstalled,
    Unknown
}

/// <summary>Optional NVIDIA stack for in-game CUDA inference (loader cannot install GPU drivers).</summary>
public enum NvidiaGpuStatus
{
    Present,
    NotFound,
    Unknown
}

public static class DependencyChecker
{
    public const string VcRedistDownloadUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
    public const string ViGEmDownloadUrl = "https://github.com/nefarius/ViGEmBus/releases";
    public const string NvidiaDriverDownloadUrl = "https://www.nvidia.com/Download/index.aspx";

    public static DependencyReport Evaluate()
    {
        var (vcOk, vcDetail) = CheckVcRedistX64();
        var (vg, vgDetail) = CheckViGEmBus();
        var (nv, nvDetail) = CheckNvidiaGpu();
        return new DependencyReport(vcOk, vcDetail, vg, vgDetail, nv, nvDetail);
    }

    private static (bool ok, string detail) CheckVcRedistX64()
    {
        try
        {
            using var k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64");
            if (k?.GetValue("Installed") is int installed && installed == 1)
            {
                var ver = k.GetValue("Version")?.ToString() ?? "?";
                return (true, $"VC++ x64 runtime present (Version registry: {ver}).");
            }
        }
        catch
        {
            // fall through
        }

        try
        {
            using var k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64");
            if (k?.GetValue("Installed") is int installed && installed == 1)
            {
                var ver = k.GetValue("Version")?.ToString() ?? "?";
                return (true, $"VC++ x64 runtime present (WOW64 view, Version: {ver}).");
            }
        }
        catch
        {
            // fall through
        }

        return (false, "Microsoft Visual C++ 2015-2022 x64 Redistributable not detected (or registry missing).");
    }

    private static (ViGEmBusStatus status, string detail) CheckViGEmBus()
    {
        try
        {
            using var sc = new ServiceController("ViGEmBus");
            sc.Refresh();
            var running = sc.Status == ServiceControllerStatus.Running;
            return running
                ? (ViGEmBusStatus.Running, "ViGEmBus service is running.")
                : (ViGEmBusStatus.Stopped, $"ViGEmBus is installed but status is {sc.Status}. Start the service or reinstall the driver.");
        }
        catch (InvalidOperationException)
        {
            return (ViGEmBusStatus.NotInstalled,
                "ViGEmBus not installed. Optional: enables ViGEm (virtual controller) input in the bot menu; Internal input works without it.");
        }
        catch (Exception ex)
        {
            return (ViGEmBusStatus.Unknown, $"Could not query services: {ex.Message}");
        }
    }

    private static (NvidiaGpuStatus status, string detail) CheckNvidiaGpu()
    {
        try
        {
            using var searcher = new ManagementObjectSearcher("SELECT Name, DriverVersion FROM Win32_VideoController");
            foreach (ManagementObject mo in searcher.Get())
            {
                using (mo)
                {
                    var name = mo["Name"]?.ToString() ?? "";
                    var ver = mo["DriverVersion"]?.ToString() ?? "";
                    if (name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase))
                    {
                        return (NvidiaGpuStatus.Present,
                            $"NVIDIA adapter: {name.Trim()} (driver {ver}). Optional for CUDA policy in the bot menu; install/update drivers from nvidia.com if CUDA is unavailable.");
                    }
                }
            }

            return (NvidiaGpuStatus.NotFound,
                "No NVIDIA GPU detected (or not reported to Windows). Optional: CUDA inference in the bot needs an NVIDIA GPU + driver; CPU inference still works.");
        }
        catch (Exception ex)
        {
            return (NvidiaGpuStatus.Unknown, $"Could not query display adapters: {ex.Message}");
        }
    }
}
