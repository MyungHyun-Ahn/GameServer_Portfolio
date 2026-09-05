using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WorldDummyClient.Configuration;

internal sealed record WorldDummySettings
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public required string WorldServerHost { get; init; }
    public required int WorldServerPort { get; init; }
    public required int PacketKey { get; init; }
    public bool UseLoginServerAuthentication { get; init; }
    public string LoginServerBaseUrl { get; init; } = "http://127.0.0.1:18080";
    public string LoginIdPrefix { get; init; } = "world_dummy_";
    public string LoginPassword { get; init; } = "WorldDummy!1234";
    public string NicknamePrefix { get; init; } = "WorldDummy";
    public required uint MapDataId { get; init; }
    public required int WorkerCount { get; init; }
    public required int VirtualUserCount { get; init; }
    public required int ConnectsPerSecond { get; init; }
    public required int FrameRate { get; init; }
    public required int RunSeconds { get; init; }
    public required int IdleMinMs { get; init; }
    public required int IdleMaxMs { get; init; }
    public required int MoveMinMs { get; init; }
    public required int MoveMaxMs { get; init; }
    public required int SyncIntervalMs { get; init; }
    public required int ResponseTimeoutMs { get; init; }
    public required int ConsoleSummaryIntervalSeconds { get; init; }
    public required int EventDrainMaxCount { get; init; }
    public required int RandomSeed { get; init; }

    public byte WorldPacketKey => checked((byte)PacketKey);
    public TimeSpan FrameInterval => TimeSpan.FromSeconds(1.0 / FrameRate);

    public string BuildLoginId(int userIndex) =>
        LoginIdPrefix + (userIndex + 1).ToString("D4", CultureInfo.InvariantCulture);

    public string BuildNickname(int userIndex) =>
        NicknamePrefix + (userIndex + 1).ToString("D4", CultureInfo.InvariantCulture);

    public static WorldDummySettings Load(string? path)
    {
        string resolvedPath = string.IsNullOrWhiteSpace(path)
            ? Path.Combine(AppContext.BaseDirectory, "appsettings.json")
            : Path.GetFullPath(path);
        if (!File.Exists(resolvedPath))
        {
            throw new FileNotFoundException("World Dummy 설정 파일을 찾을 수 없습니다.", resolvedPath);
        }

        WorldDummySettings settings = JsonSerializer.Deserialize<WorldDummySettings>(
            File.ReadAllText(resolvedPath),
            s_jsonOptions) ?? throw new InvalidDataException("World Dummy 설정 파일이 비어 있습니다.");
        settings.Validate();
        return settings;
    }

    public WorldDummySettings ApplyOverrides(CommandLineOptions options)
    {
        WorldDummySettings result = this with
        {
            MapDataId = options.MapDataId ?? MapDataId,
            VirtualUserCount = options.VirtualUserCount ?? VirtualUserCount,
            RunSeconds = options.RunSeconds ?? RunSeconds,
            RandomSeed = options.RandomSeed ?? RandomSeed
        };
        result.Validate();
        return result;
    }

    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(WorldServerHost))
        {
            throw new InvalidDataException("WorldServerHost가 비어 있습니다.");
        }
        if (WorldServerPort is <= 0 or > ushort.MaxValue)
        {
            throw new InvalidDataException("WorldServerPort는 1~65535 범위여야 합니다.");
        }
        if (PacketKey is < byte.MinValue or > byte.MaxValue)
        {
            throw new InvalidDataException("PacketKey는 0~255 범위여야 합니다.");
        }
        if (MapDataId == 0)
        {
            throw new InvalidDataException("MapDataId는 0일 수 없습니다.");
        }
        if (WorkerCount is < 1 or > 64)
        {
            throw new InvalidDataException("WorkerCount는 1~64 범위여야 합니다.");
        }
        if (VirtualUserCount is < 1 or > 4_096)
        {
            throw new InvalidDataException("VirtualUserCount는 프로세스당 1~4096 범위여야 합니다.");
        }
        if (UseLoginServerAuthentication)
        {
            if (!Uri.TryCreate(LoginServerBaseUrl, UriKind.Absolute, out Uri? loginServerUri) ||
                (loginServerUri.Scheme != Uri.UriSchemeHttp && loginServerUri.Scheme != Uri.UriSchemeHttps))
            {
                throw new InvalidDataException("LoginServerBaseUrl은 올바른 HTTP(S) URL이어야 합니다.");
            }
            if (string.IsNullOrWhiteSpace(LoginIdPrefix) || BuildLoginId(VirtualUserCount - 1).Length > 64)
            {
                throw new InvalidDataException("LoginIdPrefix로 생성되는 loginId는 1~64자여야 합니다.");
            }
            if (string.IsNullOrWhiteSpace(LoginPassword) || LoginPassword.Length > 128)
            {
                throw new InvalidDataException("LoginPassword는 1~128자여야 합니다.");
            }
            if (string.IsNullOrWhiteSpace(NicknamePrefix) || BuildNickname(VirtualUserCount - 1).Length > 64)
            {
                throw new InvalidDataException("NicknamePrefix로 생성되는 nickname은 1~64자여야 합니다.");
            }
        }
        if (ConnectsPerSecond is < 1 or > 1_000)
        {
            throw new InvalidDataException("ConnectsPerSecond는 1~1000 범위여야 합니다.");
        }
        if (FrameRate is < 10 or > 200)
        {
            throw new InvalidDataException("FrameRate는 10~200 범위여야 합니다.");
        }
        if (RunSeconds is < 1 or > 86_400)
        {
            throw new InvalidDataException("RunSeconds는 1~86400 범위여야 합니다.");
        }
        ValidateRange(IdleMinMs, IdleMaxMs, nameof(IdleMinMs), nameof(IdleMaxMs), 3_600_000);
        ValidateRange(MoveMinMs, MoveMaxMs, nameof(MoveMinMs), nameof(MoveMaxMs), 3_600_000);
        if (SyncIntervalMs is < 10 or > 60_000)
        {
            throw new InvalidDataException("SyncIntervalMs는 10~60000 범위여야 합니다.");
        }
        if (ResponseTimeoutMs is < 100 or > 300_000)
        {
            throw new InvalidDataException("ResponseTimeoutMs는 100~300000 범위여야 합니다.");
        }
        if (ConsoleSummaryIntervalSeconds is < 1 or > 3600)
        {
            throw new InvalidDataException("ConsoleSummaryIntervalSeconds는 1~3600 범위여야 합니다.");
        }
        if (EventDrainMaxCount is < 1 or > 10_000_000)
        {
            throw new InvalidDataException("EventDrainMaxCount는 1~10000000 범위여야 합니다.");
        }
    }

    private static void ValidateRange(
        int minimum,
        int maximum,
        string minimumName,
        string maximumName,
        int allowedMaximum)
    {
        if (minimum < 0 || maximum < minimum || maximum > allowedMaximum)
        {
            throw new InvalidDataException(
                $"{minimumName}/{maximumName}은 0~{allowedMaximum} 범위에서 최솟값 <= 최댓값이어야 합니다.");
        }
    }
}
