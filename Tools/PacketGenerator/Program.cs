using System.Text;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

internal static class Program
{
    private static readonly UTF8Encoding Utf8WithoutBom = new(false);

    public static int Main(string[] args)
    {
        try
        {
            string solutionRoot = FindSolutionRoot();
            string schemaRoot = Path.Combine(solutionRoot, "Packet");
            string outputRoot = Path.Combine(solutionRoot, "Generated", "Packets");

            ParseArguments(args, ref schemaRoot, ref outputRoot);

            if (!Directory.Exists(schemaRoot))
            {
                throw new InvalidOperationException($"Schema root not found: {schemaRoot}");
            }

            var deserializer = new DeserializerBuilder()
                .WithNamingConvention(HyphenatedNamingConvention.Instance)
                .Build();

            string[] schemaFiles = Directory
                .GetFiles(schemaRoot, "*.yaml", SearchOption.AllDirectories)
                .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
                .ToArray();
            if (schemaFiles.Length == 0)
            {
                throw new InvalidOperationException($"No schema files found in: {schemaRoot}");
            }

            var protocols = new List<PacketProtocolDocument>(schemaFiles.Length);
            foreach (string schemaFile in schemaFiles)
            {
                string yamlText = File.ReadAllText(schemaFile);
                PacketSchemaDocument schema = deserializer.Deserialize<PacketSchemaDocument>(yamlText)
                    ?? throw new InvalidOperationException($"Failed to deserialize schema: {schemaFile}");

                PacketSchemaValidator.Validate(schema, schemaFile);
                protocols.Add(PacketProtocolBuilder.Build(schema, schemaFile));
            }

            PacketProtocolSetValidator.Validate(protocols);
            GenerateCpp(protocols, Path.Combine(outputRoot, "Cpp"));
            GenerateCSharp(protocols, Path.Combine(outputRoot, "CSharp"));

            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"PacketGenerator failed: {exception.Message}");
            return 1;
        }
    }

    private static void GenerateCpp(IReadOnlyList<PacketProtocolDocument> protocols, string outputRoot)
    {
        var generatedPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (PacketProtocolDocument protocol in protocols)
        {
            string contentOutputDirectory = Path.Combine(outputRoot, protocol.Content);
            Directory.CreateDirectory(contentOutputDirectory);

            string packetsHeaderPath = Path.Combine(contentOutputDirectory, $"{protocol.Content}Packets.h");
            string handlerHeaderPath = Path.Combine(contentOutputDirectory, $"{protocol.Content}PacketHandler.h");

            File.WriteAllText(packetsHeaderPath, CppPacketGenerator.GeneratePacketsHeader(protocol), Utf8WithoutBom);
            File.WriteAllText(handlerHeaderPath, CppPacketGenerator.GenerateHandlerHeader(protocol), Utf8WithoutBom);
            generatedPaths.Add(Path.GetFullPath(packetsHeaderPath));
            generatedPaths.Add(Path.GetFullPath(handlerHeaderPath));

            Console.WriteLine($"Generated: {packetsHeaderPath}");
            Console.WriteLine($"Generated: {handlerHeaderPath}");
        }

        string routerHeaderPath = Path.Combine(outputRoot, "PacketRouter.h");
        File.WriteAllText(routerHeaderPath, CppPacketGenerator.GenerateRouterHeader(protocols), Utf8WithoutBom);
        generatedPaths.Add(Path.GetFullPath(routerHeaderPath));
        Console.WriteLine($"Generated: {routerHeaderPath}");

        RemoveStaleGeneratedFiles(outputRoot, "*.h", generatedPaths);
        RemoveEmptyGeneratedDirectories(outputRoot);
    }

    private static void GenerateCSharp(IReadOnlyList<PacketProtocolDocument> protocols, string outputRoot)
    {
        var generatedPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (PacketProtocolDocument protocol in protocols)
        {
            string contentOutputDirectory = Path.Combine(outputRoot, protocol.Content);
            Directory.CreateDirectory(contentOutputDirectory);

            string packetsPath = Path.Combine(contentOutputDirectory, $"{protocol.Content}Packets.g.cs");
            string handlerPath = Path.Combine(contentOutputDirectory, $"{protocol.Content}PacketHandler.g.cs");

            File.WriteAllText(packetsPath, CSharpPacketGenerator.GeneratePackets(protocol), Utf8WithoutBom);
            File.WriteAllText(handlerPath, CSharpPacketGenerator.GenerateHandler(protocol), Utf8WithoutBom);
            generatedPaths.Add(Path.GetFullPath(packetsPath));
            generatedPaths.Add(Path.GetFullPath(handlerPath));

            Console.WriteLine($"Generated: {packetsPath}");
            Console.WriteLine($"Generated: {handlerPath}");
        }

        string routerPath = Path.Combine(outputRoot, "PacketRouter.g.cs");
        File.WriteAllText(routerPath, CSharpPacketGenerator.GenerateRouter(protocols), Utf8WithoutBom);
        generatedPaths.Add(Path.GetFullPath(routerPath));
        Console.WriteLine($"Generated: {routerPath}");

        RemoveStaleGeneratedFiles(outputRoot, "*.g.cs", generatedPaths);
        RemoveEmptyGeneratedDirectories(outputRoot);
    }

    private static void RemoveStaleGeneratedFiles(
        string outputRoot,
        string searchPattern,
        IReadOnlySet<string> generatedPaths)
    {
        foreach (string existingPath in Directory.GetFiles(outputRoot, searchPattern, SearchOption.AllDirectories))
        {
            if (IsBuildArtifactPath(outputRoot, existingPath))
            {
                continue;
            }

            if (generatedPaths.Contains(Path.GetFullPath(existingPath)))
            {
                continue;
            }

            File.Delete(existingPath);
            Console.WriteLine($"Removed stale generated file: {existingPath}");
        }
    }

    private static void RemoveEmptyGeneratedDirectories(string outputRoot)
    {
        foreach (string directoryPath in Directory
            .GetDirectories(outputRoot, "*", SearchOption.AllDirectories)
            .OrderByDescending(path => path.Length))
        {
            if (!Directory.EnumerateFileSystemEntries(directoryPath).Any())
            {
                Directory.Delete(directoryPath);
            }
        }
    }

    private static bool IsBuildArtifactPath(string outputRoot, string path)
    {
        string relativePath = Path.GetRelativePath(outputRoot, path);
        string[] segments = relativePath.Split(
            new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar },
            StringSplitOptions.RemoveEmptyEntries);

        return segments.Any(segment =>
            string.Equals(segment, "bin", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(segment, "obj", StringComparison.OrdinalIgnoreCase));
    }

    private static void ParseArguments(string[] args, ref string schemaRoot, ref string outputRoot)
    {
        for (int index = 0; index < args.Length; ++index)
        {
            string argument = args[index];
            if (argument == "--schema-root" && index + 1 < args.Length)
            {
                schemaRoot = Path.GetFullPath(args[++index]);
            }
            else if (argument == "--output-root" && index + 1 < args.Length)
            {
                outputRoot = Path.GetFullPath(args[++index]);
            }
            else
            {
                throw new InvalidOperationException($"Unknown argument: {argument}");
            }
        }
    }

    private static string FindSolutionRoot()
    {
        DirectoryInfo? directory = new(AppContext.BaseDirectory);
        while (directory != null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Portfolio.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        return Directory.GetCurrentDirectory();
    }
}
