using System.IO;
using System.Text;

namespace GoyLoader.Services;

public static class LoaderSessionLog
{
    private static readonly object Sync = new();

    public static string GetGlobalLogPath()
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "GoyLoader");
        Directory.CreateDirectory(root);
        return Path.Combine(root, "GoyLoaderClient.log");
    }

    public static void WriteGlobal(string message)
    {
        WriteLine(GetGlobalLogPath(), message);
    }

    public static string GetPayloadLogPath(string payloadDir)
    {
        Directory.CreateDirectory(payloadDir);
        return Path.Combine(payloadDir, "GoyLoaderClient.log");
    }

    public static void WritePayload(string payloadDir, string message)
    {
        WriteLine(GetPayloadLogPath(payloadDir), message);
    }

    private static void WriteLine(string path, string message)
    {
        var line = $"[{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz}] {message}";
        lock (Sync)
        {
            File.AppendAllText(path, line + Environment.NewLine, Encoding.UTF8);
        }
    }
}
