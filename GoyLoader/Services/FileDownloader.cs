using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Sockets;

namespace GoyLoader.Services;

public readonly record struct DownloadProgress(long BytesReceived, long? TotalBytes);

public static class FileDownloader
{
    private const string UserAgent = "GoyLoader/1.0 (Windows; dependency setup)";

    public static async Task DownloadToFileAsync(
        Uri url,
        string filePath,
        IProgress<DownloadProgress>? progress,
        CancellationToken cancellationToken = default)
    {
        const int maxAttempts = 4;
        Exception? last = null;
        for (var attempt = 1; attempt <= maxAttempts; attempt++)
        {
            try
            {
                await DownloadToFileCoreAsync(url, filePath, progress, cancellationToken);
                return;
            }
            catch (Exception ex) when (attempt < maxAttempts
                                        && !cancellationToken.IsCancellationRequested
                                        && IsTransientNetworkFailure(ex))
            {
                last = ex;
                if (File.Exists(filePath))
                {
                    try
                    {
                        File.Delete(filePath);
                    }
                    catch
                    {
                        // ignore
                    }
                }

                var delayMs = 400 * (1 << (attempt - 1));
                await Task.Delay(delayMs, cancellationToken);
            }
        }

        throw last ?? new IOException("Download failed after retries.");
    }

    private static bool IsTransientNetworkFailure(Exception ex) =>
        ex is HttpRequestException or TaskCanceledException or IOException
            or SocketException;

    private static async Task DownloadToFileCoreAsync(
        Uri url,
        string filePath,
        IProgress<DownloadProgress>? progress,
        CancellationToken cancellationToken)
    {
        var dir = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        using var handler = new HttpClientHandler
        {
            AllowAutoRedirect = true,
            AutomaticDecompression = DecompressionMethods.All
        };
        using var client = new HttpClient(handler);
        client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("*/*"));
        client.Timeout = TimeSpan.FromMinutes(30);

        using var response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        var total = response.Content.Headers.ContentLength;
        await using var httpStream = await response.Content.ReadAsStreamAsync(cancellationToken);
        await using var fileStream = new FileStream(filePath, FileMode.Create, FileAccess.Write, FileShare.None, 81920, true);

        var buffer = new byte[81920];
        long read = 0;
        int n;
        while ((n = await httpStream.ReadAsync(buffer.AsMemory(0, buffer.Length), cancellationToken)) > 0)
        {
            await fileStream.WriteAsync(buffer.AsMemory(0, n), cancellationToken);
            read += n;
            progress?.Report(new DownloadProgress(read, total));
        }
    }
}
