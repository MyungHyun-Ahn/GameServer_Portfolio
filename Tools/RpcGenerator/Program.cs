using System.Text;
using YamlDotNet.RepresentationModel;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

internal static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            Console.OutputEncoding = new UTF8Encoding(false);
            RpcGeneratorOptions options = ParseArguments(args);
            IReadOnlyList<RpcSchemaDocument> documents = LoadDocuments(options.SchemaRoot);
            RpcSchemaValidator.ValidateSet(documents);

            RpcSchemaLock expectedLock = RpcSchemaLockManager.ValidateAndBuild(options.LockFile, documents);
            List<RpcGeneratedFile> generatedFiles = CppRpcGenerator.Generate(documents);
            RpcOutputManager.Apply(options, generatedFiles, expectedLock);

            Console.WriteLine(options.Check
                ? $"RPC generation check passed: {generatedFiles.Count} files"
                : $"RPC generation completed: {generatedFiles.Count} files");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"RpcGenerator failed: {exception.Message}");
            return 1;
        }
    }

    private static RpcGeneratorOptions ParseArguments(string[] args)
    {
        string solutionRoot = FindSolutionRoot();
        string schemaRoot = Path.Combine(solutionRoot, "Rpc");
        string outputRoot = Path.Combine(solutionRoot, "Generated", "Rpc");
        string? lockFile = null;
        bool check = false;

        for (int index = 0; index < args.Length; ++index)
        {
            string argument = args[index];
            switch (argument)
            {
            case "--schema-root":
                schemaRoot = RequirePathArgument(args, ref index, argument);
                break;
            case "--output-root":
                outputRoot = RequirePathArgument(args, ref index, argument);
                break;
            case "--lock-file":
                lockFile = RequirePathArgument(args, ref index, argument);
                break;
            case "--check":
                check = true;
                break;
            default:
                throw new InvalidOperationException($"Unknown argument: {argument}");
            }
        }

        schemaRoot = Path.GetFullPath(schemaRoot);
        outputRoot = Path.GetFullPath(outputRoot);
        lockFile = Path.GetFullPath(lockFile ?? Path.Combine(schemaRoot, "rpc-schema.lock.json"));

        if (!Directory.Exists(schemaRoot))
        {
            throw new InvalidOperationException($"Schema root not found: {schemaRoot}");
        }

        return new RpcGeneratorOptions
        {
            SchemaRoot = schemaRoot,
            OutputRoot = outputRoot,
            LockFile = lockFile,
            Check = check,
        };
    }

    private static string RequirePathArgument(string[] args, ref int index, string argument)
    {
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Missing value for {argument}");
        }

        return args[++index];
    }

    private static IReadOnlyList<RpcSchemaDocument> LoadDocuments(string schemaRoot)
    {
        string[] schemaFiles = Directory.EnumerateFiles(schemaRoot, "*", SearchOption.AllDirectories)
            .Where(path => path.EndsWith(".rpc.yaml", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".rpc.yml", StringComparison.OrdinalIgnoreCase))
            .OrderBy(path => Path.GetRelativePath(schemaRoot, path), StringComparer.Ordinal)
            .ToArray();
        if (schemaFiles.Length == 0)
        {
            throw new InvalidOperationException($"No RPC schema files found in: {schemaRoot}");
        }

        var deserializer = new DeserializerBuilder()
            .WithNamingConvention(HyphenatedNamingConvention.Instance)
            .Build();
        var documents = new List<RpcSchemaDocument>(schemaFiles.Length);
        foreach (string schemaFile in schemaFiles)
        {
            string yaml = File.ReadAllText(schemaFile, Encoding.UTF8);
            ValidateNoDuplicateMappingKeys(yaml, schemaFile);

            RpcSchemaDocument document;
            try
            {
                document = deserializer.Deserialize<RpcSchemaDocument>(yaml)
                    ?? throw new InvalidOperationException("YAML document is empty.");
            }
            catch (Exception exception)
            {
                throw new InvalidOperationException($"Invalid RPC schema '{schemaFile}': {exception.Message}", exception);
            }

            document.SourcePath = schemaFile;
            documents.Add(document);
        }

        return documents;
    }

    private static void ValidateNoDuplicateMappingKeys(string yaml, string schemaFile)
    {
        var stream = new YamlStream();
        using var reader = new StringReader(yaml);
        stream.Load(reader);
        foreach (YamlDocument document in stream.Documents)
        {
            VisitNode(document.RootNode, schemaFile, "$", new HashSet<YamlNode>(ReferenceEqualityComparer.Instance));
        }
    }

    private static void VisitNode(YamlNode node, string schemaFile, string path, HashSet<YamlNode> visited)
    {
        if (!visited.Add(node))
        {
            return;
        }

        if (node is YamlMappingNode mapping)
        {
            var keys = new HashSet<string>(StringComparer.Ordinal);
            foreach ((YamlNode keyNode, YamlNode valueNode) in mapping.Children)
            {
                string key = keyNode is YamlScalarNode scalar && scalar.Value != null
                    ? scalar.Value
                    : keyNode.ToString();
                if (!keys.Add(key))
                {
                    throw new InvalidOperationException($"Duplicate YAML key '{key}' at {path}: {schemaFile}");
                }

                VisitNode(valueNode, schemaFile, $"{path}.{key}", visited);
            }
        }
        else if (node is YamlSequenceNode sequence)
        {
            for (int index = 0; index < sequence.Children.Count; ++index)
            {
                VisitNode(sequence.Children[index], schemaFile, $"{path}[{index}]", visited);
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

        directory = new DirectoryInfo(Directory.GetCurrentDirectory());
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
