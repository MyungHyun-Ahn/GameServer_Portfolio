using System.Text.Json;

namespace WorldClientWinForms.Configuration;

internal sealed class ClientSettings
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public string WorldServerHost { get; init; } = string.Empty;
    public int WorldServerPort { get; init; }
    public int PacketKey { get; init; }
    public uint DefaultMapDataId { get; init; } = 1;
    public string LoginServerBaseUrl { get; init; } = "http://127.0.0.1:18080";
    public string LoginId { get; init; } = string.Empty;
    public string Password { get; init; } = string.Empty;

    public byte WorldPacketKey => checked((byte)PacketKey);

    public static ClientSettings Load()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "appsettings.json");
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("World Client 설정 파일을 찾을 수 없습니다.", path);
        }

        string json = File.ReadAllText(path);
        ClientSettings settings = JsonSerializer.Deserialize<ClientSettings>(json, s_jsonOptions)
            ?? throw new InvalidDataException("appsettings.json 내용이 비어 있습니다.");
        settings.Validate();
        return settings;
    }

    private void Validate()
    {
        if (!Uri.TryCreate(LoginServerBaseUrl, UriKind.Absolute, out Uri? loginServerUri) ||
            (loginServerUri.Scheme != Uri.UriSchemeHttp && loginServerUri.Scheme != Uri.UriSchemeHttps))
        {
            throw new InvalidDataException("LoginServerBaseUrl은 올바른 HTTP(S) URL이어야 합니다.");
        }

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

        if (DefaultMapDataId == 0)
        {
            throw new InvalidDataException("DefaultMapDataId는 0일 수 없습니다.");
        }
    }
}
