using System.Text;

internal static class FOutputManager
{
    private static readonly UTF8Encoding Utf8WithoutBom = new(false);

    public static void Apply(string outputRoot, IReadOnlyList<SGeneratedFile> files, bool check)
    {
        ValidateUniquePaths(files);
        if (check)
        {
            Check(outputRoot, files);
            return;
        }

        string fullOutputRoot = Path.GetFullPath(outputRoot);
        string? parentDirectory = Path.GetDirectoryName(fullOutputRoot);
        if (parentDirectory == null)
        {
            throw new InvalidOperationException($"Output root has no parent directory: {fullOutputRoot}");
        }

        Directory.CreateDirectory(parentDirectory);
        string stagingRoot = Path.Combine(parentDirectory, $".{Path.GetFileName(fullOutputRoot)}.staging.{Guid.NewGuid():N}");
        string backupRoot = Path.Combine(parentDirectory, $".{Path.GetFileName(fullOutputRoot)}.backup.{Guid.NewGuid():N}");
        Directory.CreateDirectory(stagingRoot);

        try
        {
            WriteFiles(stagingRoot, files);
            bool hadExistingOutput = Directory.Exists(fullOutputRoot);
            if (hadExistingOutput)
            {
                Directory.Move(fullOutputRoot, backupRoot);
            }

            try
            {
                Directory.Move(stagingRoot, fullOutputRoot);
            }
            catch
            {
                if (hadExistingOutput && Directory.Exists(backupRoot) && !Directory.Exists(fullOutputRoot))
                {
                    Directory.Move(backupRoot, fullOutputRoot);
                }
                throw;
            }

            if (Directory.Exists(backupRoot))
            {
                Directory.Delete(backupRoot, recursive: true);
            }
        }
        finally
        {
            if (Directory.Exists(stagingRoot))
            {
                Directory.Delete(stagingRoot, recursive: true);
            }
        }
    }

    private static void Check(string outputRoot, IReadOnlyList<SGeneratedFile> files)
    {
        if (!Directory.Exists(outputRoot))
        {
            throw new InvalidOperationException($"Generated output is missing: {outputRoot}");
        }

        Dictionary<string, string> expected = files.ToDictionary(
            file => NormalizeRelativePath(file.RelativePath),
            file => file.Content,
            StringComparer.Ordinal);
        string[] actualPaths = Directory.GetFiles(outputRoot, "*", SearchOption.AllDirectories)
            .Where(path => !IsBuildArtifactPath(outputRoot, path))
            .ToArray();
        var failures = new List<string>();

        foreach ((string relativePath, string content) in expected)
        {
            string fullPath = Path.Combine(outputRoot, relativePath.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(fullPath))
            {
                failures.Add($"missing: {relativePath}");
                continue;
            }
            string actualContent = File.ReadAllText(fullPath, Encoding.UTF8).Replace("\r\n", "\n", StringComparison.Ordinal);
            if (!string.Equals(actualContent, content, StringComparison.Ordinal))
            {
                failures.Add($"changed: {relativePath}");
            }
        }

        foreach (string actualPath in actualPaths)
        {
            string relativePath = NormalizeRelativePath(Path.GetRelativePath(outputRoot, actualPath));
            if (!expected.ContainsKey(relativePath))
            {
                failures.Add($"unexpected: {relativePath}");
            }
        }

        if (failures.Count != 0)
        {
            throw new InvalidOperationException("Generated game data is out of date:\n  " + string.Join("\n  ", failures.OrderBy(value => value, StringComparer.Ordinal)));
        }
    }

    private static void WriteFiles(string root, IReadOnlyList<SGeneratedFile> files)
    {
        foreach (SGeneratedFile file in files.OrderBy(file => file.RelativePath, StringComparer.Ordinal))
        {
            string fullPath = Path.Combine(root, file.RelativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
            File.WriteAllText(fullPath, file.Content, Utf8WithoutBom);
        }
    }

    private static void ValidateUniquePaths(IReadOnlyList<SGeneratedFile> files)
    {
        string? duplicate = files
            .GroupBy(file => NormalizeRelativePath(file.RelativePath), StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault(group => group.Count() > 1)?.Key;
        if (duplicate != null)
        {
            throw new InvalidOperationException($"Duplicate generated path: {duplicate}");
        }
    }

    private static bool IsBuildArtifactPath(string outputRoot, string path)
    {
        return Path.GetRelativePath(outputRoot, path)
            .Split([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar], StringSplitOptions.RemoveEmptyEntries)
            .Any(segment => segment.Equals("bin", StringComparison.OrdinalIgnoreCase) || segment.Equals("obj", StringComparison.OrdinalIgnoreCase));
    }

    private static string NormalizeRelativePath(string path) => path.Replace('\\', '/');
}
