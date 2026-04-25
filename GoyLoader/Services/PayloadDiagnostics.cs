using System.IO;
using System.Reflection.PortableExecutable;
using System.Text;

namespace GoyLoader.Services;

public static class PayloadDiagnostics
{
    public const string ReportFileName = "GoyPayloadReport.txt";

    public static string WriteReport(string root)
    {
        Directory.CreateDirectory(root);
        var reportPath = Path.Combine(root, ReportFileName);
        File.WriteAllText(reportPath, BuildReport(root));
        return reportPath;
    }

    private static string BuildReport(string root)
    {
        var sb = new StringBuilder();
        sb.AppendLine("=== Payload Diagnostic Report ===");
        sb.AppendLine($"Timestamp: {DateTimeOffset.Now:O}");
        sb.AppendLine($"Payload directory: {root}");
        sb.AppendLine();

        AppendInventory(sb, root);
        AppendDependencyTree(sb, root, "GoySDK.dll");
        AppendDependencyTree(sb, root, "GoySDKCore.dll");
        AppendLogTail(sb, root, "GoyLoaderBridge.log");
        AppendLogTail(sb, root, "GoySDK.log");
        AppendLogTail(sb, root, "GoySelfTest.txt");

        return sb.ToString();
    }

    private static void AppendInventory(StringBuilder sb, string root)
    {
        sb.AppendLine("Files:");
        foreach (var file in Directory.GetFiles(root).OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase))
        {
            var info = new FileInfo(file);
            sb.AppendLine($"- {info.Name} ({info.Length} bytes, {info.LastWriteTime:yyyy-MM-dd HH:mm:ss})");
        }

        foreach (var dir in Directory.GetDirectories(root).OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase))
        {
            var info = new DirectoryInfo(dir);
            sb.AppendLine($"- {info.Name}\\ (dir, {info.LastWriteTime:yyyy-MM-dd HH:mm:ss})");
        }

        sb.AppendLine();
    }

    private static void AppendDependencyTree(StringBuilder sb, string root, string rootFileName)
    {
        sb.AppendLine($"Dependencies for {rootFileName}:");
        var rootPath = Path.Combine(root, rootFileName);
        if (!File.Exists(rootPath))
        {
            sb.AppendLine($"- missing: {rootPath}");
            sb.AppendLine();
            return;
        }

        sb.AppendLine($"- {rootFileName} [payload: {rootPath}]");
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { rootFileName };
        AppendDependencyNode(sb, root, rootPath, visited, depth: 1);
        sb.AppendLine();
    }

    private static void AppendDependencyNode(StringBuilder sb, string root, string filePath, HashSet<string> visited, int depth)
    {
        IReadOnlyList<string> imports;
        try
        {
            imports = GetImportedDllNames(filePath);
        }
        catch (Exception ex)
        {
            sb.AppendLine($"{Indent(depth)}- failed to read imports: {ex.Message}");
            return;
        }

        if (imports.Count == 0)
        {
            sb.AppendLine($"{Indent(depth)}- no imports found");
            return;
        }

        foreach (var import in imports)
        {
            var resolved = ResolveDependency(root, import);
            if (!string.IsNullOrEmpty(resolved.Path))
                sb.AppendLine($"{Indent(depth)}- {import} [{resolved.Location}: {resolved.Path}]");
            else
                sb.AppendLine($"{Indent(depth)}- {import} [{resolved.Location}]");

            if (resolved.Location == "payload" &&
                !string.IsNullOrEmpty(resolved.Path) &&
                visited.Add(import) &&
                depth < 4)
            {
                AppendDependencyNode(sb, root, resolved.Path, visited, depth + 1);
            }
        }
    }

    private static (string Location, string? Path) ResolveDependency(string root, string fileName)
    {
        if (fileName.StartsWith("api-ms-win-", StringComparison.OrdinalIgnoreCase) ||
            fileName.StartsWith("ext-ms-", StringComparison.OrdinalIgnoreCase))
        {
            return ("system-api-set", null);
        }

        var payloadPath = Path.Combine(root, fileName);
        if (File.Exists(payloadPath))
            return ("payload", payloadPath);

        foreach (var dir in EnumerateSystemRoots())
        {
            var candidate = Path.Combine(dir, fileName);
            if (File.Exists(candidate))
                return ("system", candidate);
        }

        return ("missing", null);
    }

    private static IEnumerable<string> EnumerateSystemRoots()
    {
        var roots = new List<string>();
        var yielded = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void YieldIfExists(string? path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return;
            if (!Directory.Exists(path))
                return;
            if (yielded.Add(path))
                roots.Add(path);
        }

        YieldIfExists(Environment.SystemDirectory);
        YieldIfExists(Environment.GetFolderPath(Environment.SpecialFolder.Windows));

        var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        if (!string.IsNullOrWhiteSpace(windows))
            YieldIfExists(Path.Combine(windows, "SysWOW64"));

        return roots;
    }

    private static IReadOnlyList<string> GetImportedDllNames(string pePath)
    {
        using var stream = File.OpenRead(pePath);
        using var peReader = new PEReader(stream);
        var peHeader = peReader.PEHeaders.PEHeader
            ?? throw new InvalidDataException("PE header is missing.");

        var importDirectory = peHeader.ImportTableDirectory;
        if (importDirectory.RelativeVirtualAddress == 0 || importDirectory.Size == 0)
            return Array.Empty<string>();

        var importOffset = RvaToFileOffset(peReader.PEHeaders, importDirectory.RelativeVirtualAddress);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: true);

        stream.Position = importOffset;
        while (true)
        {
            var nextDescriptorOffset = stream.Position + (5 * sizeof(uint));
            var originalFirstThunk = reader.ReadUInt32();
            var timeDateStamp = reader.ReadUInt32();
            var forwarderChain = reader.ReadUInt32();
            var nameRva = reader.ReadUInt32();
            var firstThunk = reader.ReadUInt32();

            if (originalFirstThunk == 0 &&
                timeDateStamp == 0 &&
                forwarderChain == 0 &&
                nameRva == 0 &&
                firstThunk == 0)
            {
                break;
            }

            var nameOffset = RvaToFileOffset(peReader.PEHeaders, checked((int)nameRva));
            var name = ReadAsciiNullTerminated(stream, nameOffset);
            if (!string.IsNullOrWhiteSpace(name))
                names.Add(name);
            stream.Position = nextDescriptorOffset;
        }

        return names.OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    private static int RvaToFileOffset(PEHeaders headers, int rva)
    {
        foreach (var section in headers.SectionHeaders)
        {
            var size = Math.Max(section.VirtualSize, section.SizeOfRawData);
            if (rva >= section.VirtualAddress && rva < section.VirtualAddress + size)
                return rva - section.VirtualAddress + section.PointerToRawData;
        }

        throw new InvalidDataException($"RVA 0x{rva:X} does not map to a section.");
    }

    private static string ReadAsciiNullTerminated(Stream stream, int offset)
    {
        stream.Position = offset;
        var bytes = new List<byte>();
        int value;
        while ((value = stream.ReadByte()) > 0)
            bytes.Add((byte)value);
        return Encoding.ASCII.GetString(bytes.ToArray());
    }

    private static void AppendLogTail(StringBuilder sb, string root, string fileName, int maxLines = 12)
    {
        var path = Path.Combine(root, fileName);
        sb.AppendLine($"Tail of {fileName}:");
        if (!File.Exists(path))
        {
            sb.AppendLine("- (missing)");
            sb.AppendLine();
            return;
        }

        try
        {
            var lines = ReadAllLinesShared(path);
            if (lines.Length == 0)
            {
                sb.AppendLine("- (empty)");
                sb.AppendLine();
                return;
            }

            foreach (var line in lines.Skip(Math.Max(0, lines.Length - maxLines)))
                sb.AppendLine($"- {line}");
        }
        catch (Exception ex)
        {
            sb.AppendLine($"- (failed to read: {ex.Message})");
        }

        sb.AppendLine();
    }

    private static string Indent(int depth) => new(' ', depth * 2);

    private static string[] ReadAllLinesShared(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(stream);
        var lines = new List<string>();
        while (!reader.EndOfStream)
            lines.Add(reader.ReadLine() ?? string.Empty);
        return lines.ToArray();
    }
}
