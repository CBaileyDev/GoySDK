using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.ServiceProcess;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GoyLoader.Services;

public static class DependencySetupService
{
    private static readonly string CacheDir = Path.Combine(Path.GetTempPath(), "GoyLoader", "deps");

    /// <summary>Install VC++ x64 if missing (required for native DLLs / injection). Idempotent.</summary>
    public static async Task EnsureVcRedistPresentAsync(
        IProgress<string> status,
        IProgress<DownloadProgress> downloadProgress,
        CancellationToken cancellationToken = default)
    {
        if (DependencyChecker.Evaluate().VcRedistX64Ok)
        {
            status.Report("Visual C++ 2015–2022 x64 already installed.");
            return;
        }

        await DownloadAndInstallVcRedistAsync(status, downloadProgress, cancellationToken);
    }

    /// <summary>Install or start prerequisites: VC++ x64 (required for inject). ViGEmBus is optional (virtual controller only).</summary>
    public static async Task EnsurePrerequisitesAsync(
        IProgress<string> status,
        IProgress<DownloadProgress> downloadProgress,
        CancellationToken cancellationToken = default)
    {
        await EnsureVcRedistPresentAsync(status, downloadProgress, cancellationToken);

        if (DependencyChecker.Evaluate().ViGEm == ViGEmBusStatus.Running)
            return;

        if (DependencyChecker.Evaluate().ViGEm == ViGEmBusStatus.Stopped)
        {
            status.Report("Starting ViGEmBus service…");
            TryStartViGEmService();
            if (DependencyChecker.Evaluate().ViGEm == ViGEmBusStatus.Running)
                return;
        }

        try
        {
            await DownloadAndInstallViGEmBusAsync(status, downloadProgress, cancellationToken);
        }
        catch (Exception ex)
        {
            status.Report(
                $"ViGEmBus not installed or setup failed ({ex.Message}). You can still inject; use Internal input in the bot menu.");
        }
    }

    private static bool IsVcRedistSuccess(int exitCode) =>
        exitCode == 0 || exitCode == 1638 || exitCode == 3010;

    private static bool IsInstallerProbablyOk(int exitCode) =>
        exitCode == 0 || exitCode == 3010;

    public static async Task DownloadAndInstallVcRedistAsync(
        IProgress<string> status,
        IProgress<DownloadProgress> downloadProgress,
        CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(CacheDir);
        var dest = Path.Combine(CacheDir, "vc_redist.x64.exe");
        status.Report("Downloading Microsoft Visual C++ 2015–2022 x64…");

        await FileDownloader.DownloadToFileAsync(
            new Uri(DependencyChecker.VcRedistDownloadUrl),
            dest,
            downloadProgress,
            cancellationToken);

        status.Report("Installing Visual C++ Redistributable (silent)…");
        await RunInstallerAsync(dest, "/install /quiet /norestart", treatAsVcRedist: true, cancellationToken);

        for (var i = 0; i < 8; i++)
        {
            if (DependencyChecker.Evaluate().VcRedistX64Ok)
                return;
            status.Report(i == 0
                ? "Verifying VC++ install…"
                : $"Verifying VC++ install… ({i + 1}/8)");
            await Task.Delay(500, cancellationToken);
        }

        throw new InvalidOperationException("VC++ redistributable still not detected after install. Reboot and try again.");
    }

    /// <summary>If ViGEmBus is installed but stopped, start the service (no download). Call before inject for virtual-controller users.</summary>
    public static void TryStartViGEmIfInstalled(IProgress<string> status)
    {
        if (DependencyChecker.Evaluate().ViGEm != ViGEmBusStatus.Stopped)
            return;
        status.Report("Starting ViGEmBus service…");
        TryStartViGEmService();
    }

    public static async Task DownloadAndInstallViGEmBusAsync(
        IProgress<string> status,
        IProgress<DownloadProgress> downloadProgress,
        CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(CacheDir);
        status.Report("Fetching ViGEmBus download from GitHub…");

        var (url, fileName) = await GetLatestViGEmBusAssetAsync(cancellationToken);
        var dest = Path.Combine(CacheDir, fileName);

        status.Report($"Downloading {fileName}…");
        await FileDownloader.DownloadToFileAsync(url, dest, downloadProgress, cancellationToken);

        status.Report("Installing ViGEmBus driver (silent)…");
        await RunInstallerAsync(dest, "/quiet /norestart", treatAsVcRedist: false, cancellationToken);

        TryStartViGEmService();

        var vg = DependencyChecker.Evaluate().ViGEm;
        if (vg != ViGEmBusStatus.Running)
            throw new InvalidOperationException(
                "ViGEmBus is still not running. Reboot if prompted, or install manually from https://github.com/nefarius/ViGEmBus/releases");
    }

    public static void TryStartViGEmService()
    {
        try
        {
            using var sc = new ServiceController("ViGEmBus");
            sc.Refresh();
            if (sc.Status == ServiceControllerStatus.Stopped)
            {
                sc.Start();
                sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(45));
            }
        }
        catch
        {
            // caller re-checks status
        }
    }

    /// <summary>Known-good ViGEmBus release for fallback when GitHub API is rate-limited or unavailable.</summary>
    private const string ViGEmFallbackUrl =
        "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe";
    private const string ViGEmFallbackFileName = "ViGEmBus_1.22.0_x64_x86_arm64.exe";

    private static async Task<(Uri url, string fileName)> GetLatestViGEmBusAssetAsync(CancellationToken cancellationToken)
    {
        // Try the GitHub API first to get the latest release.
        try
        {
            using var handler = new HttpClientHandler { AllowAutoRedirect = true };
            using var client = new HttpClient(handler);
            client.DefaultRequestHeaders.UserAgent.ParseAdd("GoyLoader/1.0 (Windows; ViGEm setup)");
            client.Timeout = TimeSpan.FromSeconds(60);

            const string api = "https://api.github.com/repos/nefarius/ViGEmBus/releases/latest";
            GitHubRelease? release = null;
            for (var attempt = 1; attempt <= 3; attempt++)
            {
                try
                {
                    await using var stream = await client.GetStreamAsync(new Uri(api), cancellationToken);
                    release = await JsonSerializer.DeserializeAsync<GitHubRelease>(stream, cancellationToken: cancellationToken);
                    if (release?.Assets is { Count: > 0 })
                        break;
                }
                catch (Exception ex) when (attempt < 3
                                            && !cancellationToken.IsCancellationRequested
                                            && ex is HttpRequestException or TaskCanceledException)
                {
                    await Task.Delay(400 * attempt, cancellationToken);
                }
            }

            if (release?.Assets is { Count: > 0 })
            {
                var asset = release.Assets.FirstOrDefault(a =>
                    a.Name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) &&
                    a.Name.Contains("ViGEmBus", StringComparison.OrdinalIgnoreCase) &&
                    !a.Name.Contains("pdb", StringComparison.OrdinalIgnoreCase))
                    ?? release.Assets.FirstOrDefault(a => a.Name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));

                if (asset != null && !string.IsNullOrEmpty(asset.BrowserDownloadUrl))
                    return (new Uri(asset.BrowserDownloadUrl), asset.Name);
            }
        }
        catch when (!cancellationToken.IsCancellationRequested)
        {
            // Fall through to fallback URL.
        }

        // Fallback: use a known-good release URL when GitHub API fails (rate limit, network, etc.).
        return (new Uri(ViGEmFallbackUrl), ViGEmFallbackFileName);
    }

    private static async Task RunInstallerAsync(string fileName, string arguments, bool treatAsVcRedist, CancellationToken cancellationToken)
    {
        // Already running elevated (app manifest). No Verb=runas to avoid a second UAC prompt.
        var psi = new ProcessStartInfo
        {
            FileName = fileName,
            Arguments = arguments,
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Hidden
        };

        using var proc = Process.Start(psi);
        if (proc == null)
            throw new InvalidOperationException($"Failed to start: {fileName}");

        await proc.WaitForExitAsync(cancellationToken);
        var code = proc.ExitCode;

        if (treatAsVcRedist)
        {
            if (!IsVcRedistSuccess(code))
                throw new InvalidOperationException($"VC++ installer exited with code {code}.");
        }
        else
        {
            if (!IsInstallerProbablyOk(code) && code != 1638)
                throw new InvalidOperationException($"ViGEmBus installer exited with code {code}. Reboot if Windows requires it, then run this loader again.");
        }
    }

    private sealed class GitHubRelease
    {
        [JsonPropertyName("assets")]
        public List<GitHubAsset>? Assets { get; set; }
    }

    private sealed class GitHubAsset
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = "";

        [JsonPropertyName("browser_download_url")]
        public string BrowserDownloadUrl { get; set; } = "";
    }
}
