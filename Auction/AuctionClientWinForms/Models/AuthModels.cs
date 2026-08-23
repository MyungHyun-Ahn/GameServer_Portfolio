using System.Net;

namespace AuctionClientWinForms.Models;

internal sealed record AuthServerSettings(string BaseUrl);

internal sealed record LoginAccountRequest(string LoginId, string Password);

internal sealed record RegisterAccountRequest(string LoginId, string Password, string Nickname);

internal sealed record RegisterAccountResponse(ulong UserId, string Nickname);

internal sealed record AuctionServerEndpoint(string Ip, int Port);

internal sealed record LoginAccountResponse(
    ulong UserId,
    string Nickname,
    string AuctionTicket,
    int TicketExpiresInSeconds,
    AuctionServerEndpoint AuctionServer);

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
