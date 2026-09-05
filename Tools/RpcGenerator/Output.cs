using System.Text;
using System.Text.Json;

internal static class RpcOutputManager
{
    private const string ManifestName = ".rpc-generator-manifest.json";
    private static readonly UTF8Encoding Utf8NoBom = new(false);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public static void Apply(RpcGeneratorOptions options, IReadOnlyList<RpcGeneratedFile> generatedFiles, RpcSchemaLock schemaLock)
    {
        string outputRoot = Path.GetFullPath(options.OutputRoot);
        var manifest = new RpcGeneratorManifest
        {
            Version = 1,
            Files = generatedFiles.Select(file => NormalizeRelativePath(file.RelativePath)).Order(StringComparer.Ordinal).ToList(),
        };
        string manifestContent = JsonSerializer.Serialize(manifest, JsonOptions)
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n') + "\n";
        string lockContent = RpcSchemaLockManager.Serialize(schemaLock);

        RpcGeneratorManifest? oldManifest = ReadManifest(outputRoot);
        if (options.Check)
        {
            CheckFiles(outputRoot, generatedFiles, oldManifest, manifestContent);
            CheckTextFile(options.LockFile, lockContent, "RPC schema lock");
            return;
        }

        Directory.CreateDirectory(outputRoot);
        foreach (RpcGeneratedFile generatedFile in generatedFiles)
        {
            string target = ResolveSafeOutputPath(outputRoot, generatedFile.RelativePath);
            WriteChangedFile(target, generatedFile.Content);
        }

        DeleteStaleFiles(outputRoot, oldManifest, manifest.Files);
        WriteChangedFile(Path.Combine(outputRoot, ManifestName), manifestContent);
        WriteChangedFile(options.LockFile, lockContent);
    }

    private static void CheckFiles(
        string outputRoot,
        IReadOnlyList<RpcGeneratedFile> generatedFiles,
        RpcGeneratorManifest? oldManifest,
        string expectedManifest)
    {
        if (oldManifest == null)
        {
            throw new InvalidOperationException($"RPC generator manifest is missing: {Path.Combine(outputRoot, ManifestName)}");
        }

        foreach (RpcGeneratedFile generatedFile in generatedFiles)
        {
            string target = ResolveSafeOutputPath(outputRoot, generatedFile.RelativePath);
            CheckTextFile(target, generatedFile.Content, "Generated RPC file");
        }

        var expected = generatedFiles.Select(file => NormalizeRelativePath(file.RelativePath)).ToHashSet(StringComparer.Ordinal);
        foreach (string oldRelativePath in oldManifest.Files)
        {
            string normalized = NormalizeRelativePath(oldRelativePath);
            if (!expected.Contains(normalized) && File.Exists(ResolveSafeOutputPath(outputRoot, normalized)))
            {
                throw new InvalidOperationException($"Stale generated RPC file exists: {oldRelativePath}");
            }
        }

        CheckTextFile(Path.Combine(outputRoot, ManifestName), expectedManifest, "RPC generator manifest");
    }

    private static void CheckTextFile(string path, string expected, string label)
    {
        if (!File.Exists(path))
        {
            throw new InvalidOperationException($"{label} is missing: {path}");
        }

        byte[] bytes = File.ReadAllBytes(path);
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            throw new InvalidOperationException($"{label} must be UTF-8 without BOM: {path}");
        }

        string actual = Utf8NoBom.GetString(bytes);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"{label} is out of date: {path}");
        }
    }

    private static void WriteChangedFile(string path, string content)
    {
        byte[] expected = Utf8NoBom.GetBytes(content);
        if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(expected))
        {
            Console.WriteLine($"Unchanged: {path}");
            return;
        }

        string? parent = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(parent))
        {
            Directory.CreateDirectory(parent);
        }

        File.WriteAllBytes(path, expected);
        Console.WriteLine($"Generated: {path}");
    }

    private static RpcGeneratorManifest? ReadManifest(string outputRoot)
    {
        string path = Path.Combine(outputRoot, ManifestName);
        if (!File.Exists(path))
        {
            return null;
        }

        try
        {
            RpcGeneratorManifest manifest = JsonSerializer.Deserialize<RpcGeneratorManifest>(File.ReadAllText(path), JsonOptions)
                ?? throw new InvalidOperationException("Manifest is empty.");
            if (manifest.Version != 1)
            {
                throw new InvalidOperationException($"Unsupported manifest version {manifest.Version}.");
            }

            foreach (string relativePath in manifest.Files)
            {
                _ = ResolveSafeOutputPath(outputRoot, relativePath);
            }

            return manifest;
        }
        catch (Exception exception)
        {
            throw new InvalidOperationException($"Invalid RPC generator manifest '{path}': {exception.Message}", exception);
        }
    }

    private static void DeleteStaleFiles(string outputRoot, RpcGeneratorManifest? oldManifest, IReadOnlyCollection<string> expectedFiles)
    {
        if (oldManifest == null)
        {
            return;
        }

        var expected = expectedFiles.ToHashSet(StringComparer.Ordinal);
        foreach (string oldRelativePath in oldManifest.Files)
        {
            string normalized = NormalizeRelativePath(oldRelativePath);
            if (expected.Contains(normalized))
            {
                continue;
            }

            string stalePath = ResolveSafeOutputPath(outputRoot, normalized);
            if (!string.Equals(Path.GetExtension(stalePath), ".h", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException($"Refusing to delete non-header stale output: {stalePath}");
            }

            if (File.Exists(stalePath))
            {
                File.Delete(stalePath);
                Console.WriteLine($"Removed stale: {stalePath}");
            }

            RemoveEmptyParents(Path.GetDirectoryName(stalePath), outputRoot);
        }
    }

    private static void RemoveEmptyParents(string? directory, string outputRoot)
    {
        string normalizedRoot = Path.GetFullPath(outputRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        while (!string.IsNullOrEmpty(directory))
        {
            string fullDirectory = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (string.Equals(fullDirectory, normalizedRoot, StringComparison.OrdinalIgnoreCase) ||
                !Directory.Exists(fullDirectory) || Directory.EnumerateFileSystemEntries(fullDirectory).Any())
            {
                return;
            }

            Directory.Delete(fullDirectory);
            directory = Path.GetDirectoryName(fullDirectory);
        }
    }

    private static string ResolveSafeOutputPath(string outputRoot, string relativePath)
    {
        string normalized = NormalizeRelativePath(relativePath);
        if (Path.IsPathRooted(normalized) || normalized.Split('/').Any(part => part is "" or "." or ".."))
        {
            throw new InvalidOperationException($"Unsafe generated RPC path: {relativePath}");
        }

        string root = Path.GetFullPath(outputRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string candidate = Path.GetFullPath(Path.Combine(root, normalized.Replace('/', Path.DirectorySeparatorChar)));
        if (!candidate.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"Generated RPC path escapes output root: {relativePath}");
        }

        return candidate;
    }

    private static string NormalizeRelativePath(string path)
    {
        return path.Replace('\\', '/');
    }
}
