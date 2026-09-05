using System.Net.Http.Json;
using System.Text.Json;

namespace WorldClientCore.Authentication;

public sealed class WorldLoginApiClient
{
    private static readonly HttpClient s_sharedHttpClient = new();
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private readonly HttpClient m_httpClient;

    public WorldLoginApiClient(HttpClient? httpClient = null)
    {
        m_httpClient = httpClient ?? s_sharedHttpClient;
    }

    public async Task<WorldRegisterResponse> RegisterAsync(
        WorldLoginServerSettings settings,
        WorldRegisterRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ArgumentNullException.ThrowIfNull(request);

        using HttpRequestMessage httpRequest = new(HttpMethod.Post, BuildUri(settings, "/auth/register"))
        {
            Content = JsonContent.Create(new
            {
                loginId = request.LoginId,
                password = request.Password,
                nickname = request.Nickname
            })
        };

        using HttpResponseMessage response = await m_httpClient
            .SendAsync(httpRequest, cancellationToken)
            .ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw await CreateApiExceptionAsync(response, cancellationToken).ConfigureAwait(false);
        }

        RegisterSuccessEnvelope? body = await response.Content
            .ReadFromJsonAsync<RegisterSuccessEnvelope>(s_jsonOptions, cancellationToken)
            .ConfigureAwait(false);
        if (body is null || !body.Success || body.UserId == 0 || string.IsNullOrWhiteSpace(body.Nickname))
        {
            throw new InvalidOperationException("LoginServer register response was invalid.");
        }

        return new WorldRegisterResponse(body.UserId, body.Nickname);
    }

    public async Task<WorldLoginResponse> LoginAsync(
        WorldLoginServerSettings settings,
        WorldLoginRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ArgumentNullException.ThrowIfNull(request);

        using HttpRequestMessage httpRequest = new(HttpMethod.Post, BuildUri(settings, "/auth/login"))
        {
            Content = JsonContent.Create(new
            {
                loginId = request.LoginId,
                password = request.Password
            })
        };

        using HttpResponseMessage response = await m_httpClient
            .SendAsync(httpRequest, cancellationToken)
            .ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw await CreateApiExceptionAsync(response, cancellationToken).ConfigureAwait(false);
        }

        LoginSuccessEnvelope? body = await response.Content
            .ReadFromJsonAsync<LoginSuccessEnvelope>(s_jsonOptions, cancellationToken)
            .ConfigureAwait(false);
        ValidateLoginResponse(body);

        return new WorldLoginResponse(
            body!.UserId,
            body.Nickname ?? string.Empty,
            body.WorldTicket,
            body.TicketExpiresInSeconds,
            new WorldServerEndpoint(
                body.WorldServer!.Ip,
                body.WorldServer.Port,
                body.WorldServer.InstanceId));
    }

    private static Uri BuildUri(WorldLoginServerSettings settings, string path)
    {
        string baseUrl = settings.BaseUrl.Trim();
        if (!Uri.TryCreate(baseUrl.EndsWith('/') ? baseUrl : baseUrl + "/", UriKind.Absolute, out Uri? baseUri) ||
            (baseUri.Scheme != Uri.UriSchemeHttp && baseUri.Scheme != Uri.UriSchemeHttps))
        {
            throw new InvalidOperationException("LoginServer URL is invalid.");
        }

        return new Uri(baseUri, path.TrimStart('/'));
    }

    private static void ValidateLoginResponse(LoginSuccessEnvelope? body)
    {
        if (body is null || !body.Success || body.UserId == 0 ||
            string.IsNullOrWhiteSpace(body.WorldTicket) ||
            body.TicketExpiresInSeconds <= 0 ||
            body.WorldServer is null ||
            string.IsNullOrWhiteSpace(body.WorldServer.Ip) ||
            body.WorldServer.Port is <= 0 or > ushort.MaxValue ||
            body.WorldServer.InstanceId == 0)
        {
            throw new InvalidOperationException("LoginServer world response was invalid.");
        }
    }

    private static async Task<WorldLoginApiException> CreateApiExceptionAsync(
        HttpResponseMessage response,
        CancellationToken cancellationToken)
    {
        ErrorEnvelope? body = null;
        try
        {
            body = await response.Content
                .ReadFromJsonAsync<ErrorEnvelope>(s_jsonOptions, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (JsonException)
        {
        }

        return new WorldLoginApiException(
            response.StatusCode,
            body?.Code,
            body?.Message ?? $"LoginServer request failed. status={(int)response.StatusCode}");
    }

    private sealed class LoginSuccessEnvelope
    {
        public bool Success { get; init; }
        public ulong UserId { get; init; }
        public string? Nickname { get; init; }
        public string WorldTicket { get; init; } = string.Empty;
        public int TicketExpiresInSeconds { get; init; }
        public WorldServerEnvelope? WorldServer { get; init; }
    }

    private sealed class RegisterSuccessEnvelope
    {
        public bool Success { get; init; }
        public ulong UserId { get; init; }
        public string? Nickname { get; init; }
    }

    private sealed class WorldServerEnvelope
    {
        public string Ip { get; init; } = string.Empty;
        public int Port { get; init; }
        public uint InstanceId { get; init; }
    }

    private sealed class ErrorEnvelope
    {
        public string? Code { get; init; }
        public string? Message { get; init; }
    }
}
