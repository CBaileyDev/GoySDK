# P2 / 05 — Download retry loop is defeated by a 30-minute per-attempt timeout

## TL;DR
`FileDownloader.DownloadToFileAsync` has a retry loop, but each attempt uses:

```csharp
client.Timeout = TimeSpan.FromMinutes(30);
```

That means a stalled request can hang for 30 minutes before retry logic runs. The backoff budget is only a few seconds, so retries help for immediate failures but not for partial stalls.

Fix by using a smaller per-attempt timeout and adding a no-progress watchdog around each stream read.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`
- Retry loop: `DownloadToFileAsync`, current lines around 15-53.
- HTTP client setup: `DownloadToFileCoreAsync`, current lines around 69-77.
- Read loop: same function, current lines around 86-94.

## Correct Fix Strategy
Use two separate concepts:

- **Per-attempt wall-clock timeout**: how long one attempt may take before the retry loop can try again.
- **No-progress timeout**: how long a socket read may receive zero bytes before it is treated as stalled.

Recommended starting values:

- Per-attempt timeout: 90 seconds.
- No-progress timeout: 30 seconds.
- Attempts: keep current 4 unless product wants a longer total wait.

## Step 1 — Add Constants
Edit `FileDownloader.cs`.

Inside `FileDownloader`, near `UserAgent`, add:

```csharp
private static readonly TimeSpan PerAttemptTimeout = TimeSpan.FromSeconds(90);
private static readonly TimeSpan NoProgressTimeout = TimeSpan.FromSeconds(30);
```

Keep `maxAttempts = 4` unless you want to make total retry time longer.

## Step 2 — Tighten HTTP Client Timeout
Replace:

```csharp
client.Timeout = TimeSpan.FromMinutes(30);
```

with:

```csharp
client.Timeout = PerAttemptTimeout;
```

This makes a whole attempt fail in a reasonable time, allowing the existing retry loop to do useful work.

## Step 3 — Add No-Progress Watchdog
Replace the current read loop:

```csharp
var buffer = new byte[81920];
long read = 0;
int n;
while ((n = await httpStream.ReadAsync(buffer.AsMemory(0, buffer.Length), cancellationToken)) > 0)
{
    await fileStream.WriteAsync(buffer.AsMemory(0, n), cancellationToken);
    read += n;
    progress?.Report(new DownloadProgress(read, total));
}
```

with:

```csharp
var buffer = new byte[81920];
long read = 0;
while (true)
{
    using var stallCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
    stallCts.CancelAfter(NoProgressTimeout);

    int n;
    try
    {
        n = await httpStream.ReadAsync(buffer.AsMemory(0, buffer.Length), stallCts.Token);
    }
    catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
    {
        throw new IOException(
            $"Download stalled: no bytes received for {NoProgressTimeout.TotalSeconds:0} seconds.");
    }

    if (n <= 0)
        break;

    await fileStream.WriteAsync(buffer.AsMemory(0, n), cancellationToken);
    read += n;
    progress?.Report(new DownloadProgress(read, total));
}
```

Do not add an unused `lastProgressUtc` variable. The linked cancellation token is the watchdog.

## Step 4 — Keep Partial-File Cleanup
The current retry loop deletes the partial file after transient failures:

```csharp
if (File.Exists(filePath))
{
    try { File.Delete(filePath); }
    catch { }
}
```

Keep this. The new stall exception is an `IOException`, and `IsTransientNetworkFailure` already treats `IOException` as retryable.

## Step 5 — Optional: Report Retry Status
`FileDownloader` currently has only byte progress, not status text. If the UI needs better feedback, do not print from the downloader directly. Instead either:

- extend the downloader signature with an optional `IProgress<string> status`, or
- let callers report before retrying.

This is optional. The functional fix is timeout/watchdog behavior.

## Step 6 — Pair With TLS and Signature Work
If **P0/04** is being applied in the same pass, also update the `HttpClientHandler` here:

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

This is a transport hardening improvement. It does not replace Authenticode verification.

## Verification
1. Build:
   ```bash
   dotnet build /Users/carterbarker/Documents/GoySDK/GoyLoader/GoyLoader.csproj
   ```

2. Stalled-body test:
   - Run a local HTTP server that sends headers and then never sends body bytes.
   - Trigger a download.
   - Expected: each attempt fails after about 30 seconds of no body progress, then retries.

3. Slow-but-working test:
   - Serve a file slowly but with at least one chunk every few seconds.
   - Expected: download continues as long as bytes arrive before `NoProgressTimeout`.

4. Total attempt timeout test:
   - Serve bytes slowly enough that the overall attempt exceeds 90 seconds.
   - Expected: attempt times out and retry logic runs.

5. Normal download regression:
   - Download VC++ or ViGEmBus over a normal connection.
   - Expected: progress reports and successful completion.

6. Cancellation test:
   - Cancel the provided `cancellationToken`.
   - Expected: cancellation propagates and is not converted into a retryable stall exception.

## Don't Do
- Do not reduce `client.Timeout` to a very small value such as 10 seconds; legitimate slow downloads may never complete.
- Do not remove retries.
- Do not use `Content-Length` as a stall detector; servers may omit it.
- Do not leave unused watchdog variables in the loop.

## Related
- **P0/04** — downloaded installers must also be verified before execution.
