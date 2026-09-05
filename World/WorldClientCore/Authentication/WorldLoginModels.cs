using System.Net;

namespace WorldClientCore.Authentication;

public sealed record WorldLoginServerSettings(string BaseUrl);

public sealed record WorldLoginRequest(string LoginId, string Password);

public sealed record WorldRegisterRequest(string LoginId, string Password, string Nickname);

public sealed record WorldRegisterResponse(ulong UserId, string Nickname);

public sealed record WorldServerEndpoint(string Ip, int Port, uint InstanceId);

public sealed record WorldLoginResponse(
    ulong UserId,
    string Nickname,
    string WorldTicket,
    int TicketExpiresInSeconds,
    WorldServerEndpoint WorldServer);

public sealed class WorldLoginApiException : Exception
{
    public WorldLoginApiException(HttpStatusCode statusCode, string? errorCode, string message)
        : base(message)
    {
        StatusCode = statusCode;
        ErrorCode = errorCode;
    }

    public HttpStatusCode StatusCode { get; }

    public string? ErrorCode { get; }
}
