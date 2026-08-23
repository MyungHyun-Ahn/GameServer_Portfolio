using System.Net.Http.Json;
using System.Text.Json;
using ChattingClientWinForms.Models;

namespace ChattingClientWinForms.Networking;

internal sealed class AuthApiClient
{
    private static readonly HttpClient s_httpClient = new();
    private static readonly JsonSerializerOptions s_jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public async Task<RegisterAccountResponse> RegisterAsync(
        AuthServerSettings settings,
        RegisterAccountRequest request,
        CancellationToken cancellationToken = default)
    {
        using HttpRequestMessage httpRequest = new(HttpMethod.Post, BuildUri(settings, "/auth/register"))
        {
            Content = JsonContent.Create(new
            {
                loginId = request.LoginId,
                password = request.Password,
                nickname = request.Nickname
            })
        };

        using HttpResponseMessage response = await s_httpClient.SendAsync(httpRequest, cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw await CreateApiExceptionAsync(response, cancellationToken).ConfigureAwait(false);
        }

        RegisterSuccessEnvelope? body = await response.Content
            .ReadFromJsonAsync<RegisterSuccessEnvelope>(s_jsonOptions, cancellationToken)
            .ConfigureAwait(false);
        if (body is null)
        {
            throw new InvalidOperationException("LoginServer register response was empty.");
        }

        return new RegisterAccountResponse(body.UserId, body.Nickname ?? string.Empty);
    }

    public async Task<LoginAccountResponse> LoginAsync(
        AuthServerSettings settings,
        LoginAccountRequest request,
        CancellationToken cancellationToken = default)
    {
        using HttpRequestMessage httpRequest = new(HttpMethod.Post, BuildUri(settings, "/auth/login"))
        {
            Content = JsonContent.Create(new
            {
                loginId = request.LoginId,
                password = request.Password
            })
        };

        using HttpResponseMessage response = await s_httpClient.SendAsync(httpRequest, cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw await CreateApiExceptionAsync(response, cancellationToken).ConfigureAwait(false);
        }

        LoginSuccessEnvelope? body = await response.Content
            .ReadFromJsonAsync<LoginSuccessEnvelope>(s_jsonOptions, cancellationToken)
            .ConfigureAwait(false);
        if (body?.ChatServer is null)
        {
            throw new InvalidOperationException("LoginServer login response was invalid.");
        }

        return new LoginAccountResponse(
            body.UserId,
            body.Nickname ?? string.Empty,
            body.Ticket ?? string.Empty,
            body.TicketExpiresInSeconds,
            new ChatServerEndpoint(body.ChatServer.Ip ?? "127.0.0.1", body.ChatServer.Port));
    }

    private static Uri BuildUri(AuthServerSettings settings, string path)
    {
        string normalizedBaseUrl = settings.BaseUrl.Trim();
        if (!Uri.TryCreate(normalizedBaseUrl.EndsWith('/') ? normalizedBaseUrl : normalizedBaseUrl + "/", UriKind.Absolute, out Uri? baseUri))
        {
            throw new InvalidOperationException("LoginServer base URL is invalid.");
        }

        return new Uri(baseUri, path.TrimStart('/'));
    }

    private static async Task<AuthApiException> CreateApiExceptionAsync(
        HttpResponseMessage response,
        CancellationToken cancellationToken)
    {
        LoginServerErrorEnvelope? body = null;
        try
        {
            body = await response.Content.ReadFromJsonAsync<LoginServerErrorEnvelope>(s_jsonOptions, cancellationToken).ConfigureAwait(false);
        }
        catch (JsonException)
        {
        }

        string message = body?.Message ?? $"LoginServer request failed. status={(int)response.StatusCode}";
        return new AuthApiException(response.StatusCode, body?.Code, message);
    }

    private sealed class RegisterSuccessEnvelope
    {
        public bool Success { get; init; }

        public uint UserId { get; init; }

        public string? Nickname { get; init; }
    }

    private sealed class LoginSuccessEnvelope
    {
        public bool Success { get; init; }

        public uint UserId { get; init; }

        public string? Nickname { get; init; }

        public string? Ticket { get; init; }

        public int TicketExpiresInSeconds { get; init; }

        public ChatServerEnvelope? ChatServer { get; init; }
    }

    private sealed class ChatServerEnvelope
    {
        public string? Ip { get; init; }

        public int Port { get; init; }
    }

    private sealed class LoginServerErrorEnvelope
    {
        public bool Success { get; init; }

        public string? Code { get; init; }

        public string? Message { get; init; }
    }
}
