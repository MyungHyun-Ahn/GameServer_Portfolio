using System.Text;

internal static class Program
{
    public static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        try
        {
            FGameDataGeneratorOptions options = FGameDataGeneratorOptions.Parse(args);
            if (options.SelfTest)
            {
                FGameDataGeneratorSelfTest.Run();
                Console.WriteLine("GameDataGenerator self-test passed.");
                return 0;
            }

            return Run(options);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"GameDataGenerator failed: {exception.Message}");
            return 1;
        }
    }

    private static int Run(FGameDataGeneratorOptions options)
    {
        if (!Directory.Exists(options.InputRoot))
        {
            throw new InvalidOperationException($"Input root not found: {options.InputRoot}");
        }

        var diagnostics = new FDiagnosticBag();
        IReadOnlyList<FGameDataTable> tables = FWorkbookParser.ParseDirectory(options.InputRoot, diagnostics);
        IReadOnlyDictionary<string, FGameDataEnumDefinition> enumDefinitions =
            FGameDataValidator.Validate(tables, diagnostics);
        if (diagnostics.HasErrors)
        {
            PrintDiagnostics(diagnostics);
            Console.Error.WriteLine($"Validation failed with {diagnostics.Count} error(s). No generated files were changed.");
            return 1;
        }

        Console.WriteLine($"Validated {tables.Count} table(s) from {options.InputRoot}.");
        if (options.ValidateOnly)
        {
            Console.WriteLine("Validation completed. Output generation was skipped (--validate-only).");
            return 0;
        }

        IReadOnlyList<SGeneratedFile> files = FGameDataGenerators.Generate(tables, enumDefinitions);
        FOutputManager.Apply(options.OutputRoot, files, options.Check);
        Console.WriteLine(options.Check
            ? $"Game-data generation check passed: {files.Count} file(s)."
            : $"Generated {files.Count} file(s) atomically under {options.OutputRoot}.");
        return 0;
    }

    private static void PrintDiagnostics(FDiagnosticBag diagnostics)
    {
        foreach (SGameDataDiagnostic diagnostic in diagnostics.GetSorted())
        {
            Console.Error.WriteLine(diagnostic);
        }
    }
}

internal sealed class FGameDataGeneratorOptions
{
    public required string InputRoot { get; init; }

    public required string OutputRoot { get; init; }

    public required bool Check { get; init; }

    public required bool ValidateOnly { get; init; }

    public required bool SelfTest { get; init; }

    public static FGameDataGeneratorOptions Parse(string[] args)
    {
        string solutionRoot = FindSolutionRoot();
        string inputRoot = Path.Combine(solutionRoot, "GameData", "Excel");
        string outputRoot = Path.Combine(solutionRoot, "Generated", "GameData");
        bool check = false;
        bool validateOnly = false;
        bool selfTest = false;

        for (int index = 0; index < args.Length; ++index)
        {
            switch (args[index])
            {
                case "--input-root":
                    inputRoot = RequirePathArgument(args, ref index, "--input-root");
                    break;
                case "--output-root":
                    outputRoot = RequirePathArgument(args, ref index, "--output-root");
                    break;
                case "--check":
                    check = true;
                    break;
                case "--validate-only":
                    validateOnly = true;
                    break;
                case "--self-test":
                    selfTest = true;
                    break;
                default:
                    throw new InvalidOperationException($"Unknown argument: {args[index]}");
            }
        }

        if (check && validateOnly)
        {
            throw new InvalidOperationException("--check and --validate-only cannot be used together.");
        }
        if (selfTest && args.Length != 1)
        {
            throw new InvalidOperationException("--self-test cannot be combined with other arguments.");
        }

        return new FGameDataGeneratorOptions
        {
            InputRoot = Path.GetFullPath(inputRoot),
            OutputRoot = Path.GetFullPath(outputRoot),
            Check = check,
            ValidateOnly = validateOnly,
            SelfTest = selfTest,
        };
    }

    private static string RequirePathArgument(string[] args, ref int index, string argument)
    {
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Missing value for {argument}.");
        }

        return args[++index];
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
