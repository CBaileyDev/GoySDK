# P2 / 05 — Download retry budget (3.2s) dwarfed by per-attempt 30-min timeout

## TL;DR
`FileDownloader.DownloadToFileAsync` configures a 4-attempt retry loop with exponential backoff (`400 * (1 << (attempt - 1))` ms), giving a total backoff of `400 + 800 + 1600 = 2.8s` between attempts. But each attempt's `HttpClient.Timeout = TimeSpan.FromMinutes(30)`, so a single hung request blocks the loader for 30 minutes before the retry logic ever sees a failure. The retry exists in name only.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`
- Retry loop: lines **23–50** (`DownloadToFileAsync`)
- Per-attempt timeout: line **77** (`client.Timeout = TimeSpan.FromMinutes(30);`)

## Problem
A typical small dependency installer (`vc_redist.x64.exe` is ~25MB) on a working connection completes in seconds. A degraded connection (slow but not broken) can stall progress to a few KB/s; on a 25MB download that's 30+ minutes. The 30-min `Timeout` was chosen for the worst-case slow connection, not for "give up and retry."

The result: when the connection genuinely fails (no DNS, server 5xx, TCP RST), the retry kicks in fine. When the connection *partially* fails (slow, intermittent, or stalled mid-stream), the user stares at a hung loader for 30 minutes.

## Why it matters
Loader UX. Users blame the loader rather than their connection.

## Root cause
Two unrelated knobs (overall download timeout vs. per-attempt timeout) collapsed into one. The per-attempt timeout should be much smaller; the *total* time budget should be the larger value.

## Fix

### Step 1 — Add a no-progress watchdog

Edit `/Users/carterbarker/Documents/GoySDK/GoyLoader/Services/FileDownloader.cs`. Find the constants at the top of `DownloadToFileCoreAsync` (lines 65–77):

```csharp
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
```

Replace with:

```cpp
var dir = Path.GetDirectoryName(filePath);
if (!string.IsNullOrEmpty(dir))
    Directory.CreateDirectory(dir);

using var handler = new HttpClientHandler
{
    AllowAutoRedirect = true,
    AutomaticDecompression = DecompressionMethods.All,
};
using var client = new HttpClient(handler);
client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("*/*"));
// Per-attempt time budget: caller can retry. 90s is enough for normal ~25MB
// downloads on slow but working connections; truly bad connections will fail
// fast and let the retry loop try again.
client.Timeout = TimeSpan.FromSeconds(90);
```

(The above uses C# syntax — remove the `cpp` after `replace with` block tag if your editor renders this as a literal code block.)

### Step 2 — Add a no-progress watchdog inside the read loop

Find the read loop (lines 86–94):

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

Replace with:

```csharp
var buffer = new byte[81920];
long read = 0;
int n;
var lastProgressUtc = DateTime.UtcNow;
const int kStallSeconds = 30;
while (true)
{
    using var stallCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
    stallCts.CancelAfter(TimeSpan.FromSeconds(kStallSeconds));
    try
    {
        n = await httpStream.ReadAsync(buffer.AsMemory(0, buffer.Length), stallCts.Token);
    }
    catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
    {
        throw new IOException(
            $"Download stalled — no bytes received in {kStallSeconds} seconds.");
    }
    if (n <= 0) break;

    await fileStream.WriteAsync(buffer.AsMemory(0, n), cancellationToken);
    read += n;
    lastProgressUtc = DateTime.UtcNow;
    progress?.Report(new DownloadProgress(read, total));
}
```

(`lastProgressUtc` is overwritten and not read elsewhere in this snippet — leave it for future use, or remove if your linter complains.)

### Step 3 — Increase the retry attempt count and improve the backoff

The current loop tries 4 times. With Step 1's tighter timeout, a "genuinely down" remote will be detected in ~90s + backoff, so 4 attempts in ~5 minutes is fine. If you want to be more patient, increase `maxAttempts`:

In `DownloadToFileAsync` (line 21), find:
```csharp
const int maxAttempts = 4;
```
Optionally bump to:
```csharp
const int maxAttempts = 6;  // ~9.5 min total worst-case retry budget with 90s per-attempt
```

(Optional — the original 4 is fine if you don't want to wait longer.)

### Step 4 — Make the retry decision smarter on stall vs. hard-fail

Find (lines 30–32):
```csharp
catch (Exception ex) when (attempt < maxAttempts
                            && !cancellationToken.IsCancellationRequested
                            && IsTransientNetworkFailure(ex))
```

`IsTransientNetworkFailure` already includes `IOException`, which catches the new stall throw from Step 2. No change needed.

## Verification

1. **Build** — `dotnet build`.
2. **Stall test** — run a local HTTP server that accepts the connection but never sends a body. Trigger a download. Pre-fix: hangs 30 minutes. Post-fix: throws "Download stalled" after ~30 seconds, retry kicks in, eventually fails out cleanly.
3. **Slow-but-working test** — use `iptables`/`tc`/Network Link Conditioner to throttle the loopback connection to 100 KB/s. Confirm a 1 MB file downloads successfully (the per-attempt 90s budget is enough for >100 KB/s × 90 = 9 MB).
4. **Regression** — normal-network download of `vc_redist.x64.exe` still completes in <30s.

## Don't do

- Do not set `client.Timeout` to a very low value (e.g., 10s). On legitimately-slow connections, that triggers retries that themselves can't complete, producing a worse experience than "wait 30 min."
- Do not remove the retry loop. The retry exists for transient packet loss / DNS hiccups, which are common on home networks.
- Do not make the stall detector trigger on the `total` byte count. The server may not send `Content-Length`; rely on "no bytes received in N seconds" as the universal stall signal.

## Related
- **P0/04** — same downloader is used to fetch installers that should also be Authenticode-verified. The retry/timeout fix here doesn't substitute for the verification fix there.
