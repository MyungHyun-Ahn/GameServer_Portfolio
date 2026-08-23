using System.Net.Http.Json;
using System.Text.Json;
using AuctionClientWinForms.Models;

namespace AuctionClientWinForms.Networking;

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
        if (body?.AuctionServer is null || string.IsNullOrWhiteSpace(body.AuctionTicket))
        {
            throw new InvalidOperationException("LoginServer auction response was invalid.");
        }

        return new LoginAccountResponse(
            body.UserId,
            body.Nickname ?? string.Empty,
            body.AuctionTicket,
            body.TicketExpiresInSeconds,
            new AuctionServerEndpoint(body.AuctionServer.Ip ?? "127.0.0.1", body.AuctionServer.Port));
    }

    private static Uri BuildUri(AuthServerSettings settings, string path)
    {
        string baseUrl = settings.BaseUrl.Trim();
        if (!Uri.TryCreate(baseUrl.EndsWith('/') ? baseUrl : baseUrl + "/", UriKind.Absolute, out Uri? baseUri))
        {
            throw new InvalidOperationException("LoginServer URL이 올바르지 않습니다.");
        }

        return new Uri(baseUri, path.TrimStart('/'));
    }

    private static async Task<AuthApiException> CreateApiExceptionAsync(
        HttpResponseMessage response,
        CancellationToken cancellationToken)
    {
        ErrorEnvelope? body = null;
        try
        {
            body = await response.Content.ReadFromJsonAsync<ErrorEnvelope>(s_jsonOptions, cancellationToken).ConfigureAwait(false);
        }
        catch (JsonException)
        {
        }

        return new AuthApiException(
            response.StatusCode,
            body?.Code,
            body?.Message ?? $"LoginServer request failed. status={(int)response.StatusCode}");
    }

    private sealed class LoginSuccessEnvelope
    {
        public ulong UserId { get; init; }
        public string? Nickname { get; init; }
        public string AuctionTicket { get; init; } = string.Empty;
        public int TicketExpiresInSeconds { get; init; }
        public AuctionServerEnvelope? AuctionServer { get; init; }
    }

    private sealed class RegisterSuccessEnvelope
    {
        public ulong UserId { get; init; }
        public string? Nickname { get; init; }
    }

    private sealed class AuctionServerEnvelope
    {
        public string? Ip { get; init; }
        public int Port { get; init; }
    }

    private sealed class ErrorEnvelope
    {
        public string? Code { get; init; }
        public string? Message { get; init; }
    }
}
