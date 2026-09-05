namespace WorldDummyClient.Configuration;

internal sealed record CommandLineOptions(
    string? ConfigPath,
    uint? MapDataId,
    int? VirtualUserCount,
    int? RunSeconds,
    int? RandomSeed,
    bool ValidateConfig)
{
    public static CommandLineOptions Parse(IReadOnlyList<string> args)
    {
        string? configPath = null;
        uint? mapDataId = null;
        int? virtualUserCount = null;
        int? runSeconds = null;
        int? randomSeed = null;
        bool validateConfig = false;

        for (int index = 0; index < args.Count; ++index)
        {
            string argument = args[index];
            switch (argument.ToLowerInvariant())
            {
                case "--config":
                    configPath = ReadValue(args, ref index, argument);
                    break;
                case "--map-data-id":
                    mapDataId = ParseUInt32(ReadValue(args, ref index, argument), argument);
                    break;
                case "--virtual-users":
                    virtualUserCount = ParseInt32(ReadValue(args, ref index, argument), argument);
                    break;
                case "--run-seconds":
                    runSeconds = ParseInt32(ReadValue(args, ref index, argument), argument);
                    break;
                case "--random-seed":
                    randomSeed = ParseInt32(ReadValue(args, ref index, argument), argument);
                    break;
                case "--validate-config":
                    validateConfig = true;
                    break;
                default:
                    throw new ArgumentException($"알 수 없는 인자입니다: {argument}");
            }
        }

        return new CommandLineOptions(
            configPath,
            mapDataId,
            virtualUserCount,
            runSeconds,
            randomSeed,
            validateConfig);
    }

    private static string ReadValue(IReadOnlyList<string> args, ref int index, string argument)
    {
        if (++index >= args.Count)
        {
            throw new ArgumentException($"{argument} 뒤에 값이 필요합니다.");
        }
        return args[index];
    }

    private static int ParseInt32(string value, string argument) =>
        int.TryParse(value, out int parsed)
            ? parsed
            : throw new ArgumentException($"{argument} 값이 정수가 아닙니다: {value}");

    private static uint ParseUInt32(string value, string argument) =>
        uint.TryParse(value, out uint parsed)
            ? parsed
            : throw new ArgumentException($"{argument} 값이 양의 정수가 아닙니다: {value}");
}
