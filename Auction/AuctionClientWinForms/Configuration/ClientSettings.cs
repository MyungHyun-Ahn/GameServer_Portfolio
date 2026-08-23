using System.Text.Json;

namespace AuctionClientWinForms.Configuration;

internal sealed class ClientSettings
{
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public string LoginServerBaseUrl { get; init; } = string.Empty;

    public int PacketKey { get; init; }

    public byte AuctionPacketKey => checked((byte)PacketKey);

    public static ClientSettings Load()
    {
        string path = Path.Combine(AppContext.BaseDirectory, "appsettings.json");
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("Auction Client 설정 파일을 찾을 수 없습니다.", path);
        }

        string json = File.ReadAllText(path);
        ClientSettings settings = JsonSerializer.Deserialize<ClientSettings>(json, s_jsonOptions)
            ?? throw new InvalidDataException("appsettings.json 내용이 비어 있습니다.");
        settings.Validate();
        return settings;
    }

    private void Validate()
    {
        if (!Uri.TryCreate(LoginServerBaseUrl, UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            throw new InvalidDataException("LoginServerBaseUrl은 유효한 HTTP(S) 주소여야 합니다.");
        }

        if (PacketKey is < byte.MinValue or > byte.MaxValue)
        {
            throw new InvalidDataException("PacketKey는 0~255 범위여야 합니다.");
        }
    }
}
