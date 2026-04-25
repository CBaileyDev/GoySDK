using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;

namespace GoyLoader.Services;

public static class PayloadExtractor
{
    private const string ResourceName = "GoyLoader.Payload.zip";

    public static bool IsPayloadEmbedded()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName);
        return stream != null;
    }

    public static string ExtractPayloadToCache()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException(
                "Embedded payload is missing. Run scripts/Package-Payload.ps1 after building internal_bot, then rebuild GoyLoader.");

        using var ms = new MemoryStream();
        stream.CopyTo(ms);
        var bytes = ms.ToArray();
        var hash = Convert.ToHexString(SHA256.HashData(bytes))[..16];
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "GoyLoader", "LoaderCache", hash);
        Directory.CreateDirectory(root);

        var sentinel = Path.Combine(root, ".extracted");
        var dllPath = Path.Combine(root, "GoySDK.dll");
        if (File.Exists(sentinel) && File.Exists(dllPath))
        {
            VerifyExtractedLayout(root);
            PayloadDiagnostics.WriteReport(root);
            return root;
        }

        var locked = new List<string>();
        foreach (var f in Directory.EnumerateFileSystemEntries(root))
        {
            try
            {
                if (File.Exists(f))
                    File.Delete(f);
                else
                    Directory.Delete(f, true);
            }
            catch (IOException)
            {
                locked.Add(Path.GetFileName(f));
            }
            catch
            {
                // best effort clean before re-extract
            }
        }

        if (locked.Count > 0)
            throw new IOException(
                $"Cannot update payload — files are locked (likely by a running game): {string.Join(", ", locked)}.\n" +
                "Close the host game and try again.");

        using (var zip = new ZipArchive(new MemoryStream(bytes), ZipArchiveMode.Read))
        {
            zip.ExtractToDirectory(root, overwriteFiles: true);
        }

        if (!File.Exists(dllPath))
            throw new InvalidOperationException("Payload did not contain GoySDK.dll at archive root.");

        File.WriteAllText(sentinel, DateTime.UtcNow.ToString("O"));
        VerifyExtractedLayout(root);
        PayloadDiagnostics.WriteReport(root);
        return root;
    }

    /// <summary>Ensures the unpacked payload can load the injector chain (GoySDK → GoySDKCore).</summary>
    public static void VerifyExtractedLayout(string root)
    {
        var sdk = Path.Combine(root, "GoySDK.dll");
        var core = Path.Combine(root, "GoySDKCore.dll");
        if (!File.Exists(sdk))
            throw new FileNotFoundException("Payload is missing GoySDK.dll. Rebuild with scripts/Package-Payload.ps1.", sdk);
        if (!File.Exists(core))
            throw new FileNotFoundException(
                "Payload is missing GoySDKCore.dll (required by the injector). Rebuild internal_bot and re-package the payload.",
                core);

        // CUDA LibTorch payloads include torch_cuda.dll; if present, require paired CUDA shim DLLs.
        var torchCuda = Path.Combine(root, "torch_cuda.dll");
        if (File.Exists(torchCuda))
        {
            var c10Cuda = Path.Combine(root, "c10_cuda.dll");
            if (!File.Exists(c10Cuda))
                throw new FileNotFoundException(
                    "Payload appears CUDA-enabled (torch_cuda.dll) but c10_cuda.dll is missing. Re-package from a full CUDA LibTorch build output.",
                    c10Cuda);
        }
    }
}
