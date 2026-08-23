using System.Net;

namespace ChattingClientWinForms.Models;

internal sealed record AuthServerSettings(string BaseUrl);

internal sealed record RegisterAccountRequest(string LoginId, string Password, string Nickname);

internal sealed record RegisterAccountResponse(uint UserId, string Nickname);

internal sealed record LoginAccountRequest(string LoginId, string Password);

internal sealed record ChatServerEndpoint(string Ip, int Port);

internal sealed record LoginAccountResponse(
    uint UserId,
    string Nickname,
    string Ticket,
    int TicketExpiresInSeconds,
    ChatServerEndpoint ChatServer);

internal sealed record LoginServerErrorResponse(string? Code, string? Message);

internal sealed class AuthApiException : Exception
{
    public AuthApiException(HttpStatusCode statusCode, string? errorCode, string message)
        : base(message)
    {
        StatusCode = statusCode;
        ErrorCode = errorCode;
    }

    public HttpStatusCode StatusCode { get; }

    public string? ErrorCode { get; }
}
