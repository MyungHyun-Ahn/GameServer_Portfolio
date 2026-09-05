using System.Net;
using System.Text;
using System.Text.Json;
using WorldClientCore.Authentication;

namespace WorldClientWinForms.SmokeTests;

internal static class WorldLoginApiClientSelfTest
{
    public static async Task<int> RunAsync()
    {
        try
        {
            await VerifySuccessResponseAsync();
            await VerifyErrorResponseAsync();
            await VerifyInvalidWorldResponseAsync();
            await VerifyRegisterSuccessResponseAsync();
            await VerifyRegisterErrorResponseAsync();
            await VerifyInvalidRegisterResponseAsync();
            Console.WriteLine("[World login API self-test] PASS");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[World login API self-test] FAIL: {exception}");
            return 1;
        }
    }

    private static async Task VerifySuccessResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler(async (request, cancellationToken) =>
        {
            Ensure(request.Method == HttpMethod.Post, "Login request must use POST.");
            Ensure(request.RequestUri == new Uri("http://login.test:18080/auth/login"), "Login request URI mismatch.");
            string payload = await request.Content!.ReadAsStringAsync(cancellationToken);
            using JsonDocument document = JsonDocument.Parse(payload);
            Ensure(document.RootElement.GetProperty("loginId").GetString() == "tester", "loginId payload mismatch.");
            Ensure(document.RootElement.GetProperty("password").GetString() == "secret", "password payload mismatch.");

            return JsonResponse(HttpStatusCode.OK,
                """
                {
                  "success": true,
                  "userId": 42,
                  "nickname": "World Tester",
                  "worldTicket": "world-ticket",
                  "ticketExpiresInSeconds": 60,
                  "worldServer": { "ip": "127.0.0.2", "port": 19200, "instanceId": 7 }
                }
                """);
        }));
        var client = new WorldLoginApiClient(httpClient);

        WorldLoginResponse response = await client.LoginAsync(
            new WorldLoginServerSettings("http://login.test:18080"),
            new WorldLoginRequest("tester", "secret"));

        Ensure(response.UserId == 42, "userId parse mismatch.");
        Ensure(response.WorldTicket == "world-ticket", "worldTicket parse mismatch.");
        Ensure(response.WorldServer == new WorldServerEndpoint("127.0.0.2", 19200, 7), "worldServer parse mismatch.");
    }

    private static async Task VerifyErrorResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler((_, _) => Task.FromResult(
            JsonResponse(HttpStatusCode.Unauthorized,
                """{"success":false,"code":"INVALID_CREDENTIALS","message":"invalid credentials"}"""))));
        var client = new WorldLoginApiClient(httpClient);

        try
        {
            await client.LoginAsync(
                new WorldLoginServerSettings("http://login.test"),
                new WorldLoginRequest("tester", "bad"));
            throw new InvalidOperationException("HTTP error response was accepted.");
        }
        catch (WorldLoginApiException exception)
        {
            Ensure(exception.StatusCode == HttpStatusCode.Unauthorized, "HTTP status mapping mismatch.");
            Ensure(exception.ErrorCode == "INVALID_CREDENTIALS", "LoginServer error code mapping mismatch.");
        }
    }

    private static async Task VerifyInvalidWorldResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler((_, _) => Task.FromResult(
            JsonResponse(HttpStatusCode.OK,
                """{"success":true,"userId":42,"worldTicket":"","ticketExpiresInSeconds":60}"""))));
        var client = new WorldLoginApiClient(httpClient);

        try
        {
            await client.LoginAsync(
                new WorldLoginServerSettings("http://login.test"),
                new WorldLoginRequest("tester", "secret"));
            throw new InvalidOperationException("Invalid world response was accepted.");
        }
        catch (InvalidOperationException exception) when (
            exception.Message == "LoginServer world response was invalid.")
        {
        }
    }

    private static async Task VerifyRegisterSuccessResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler(async (request, cancellationToken) =>
        {
            Ensure(request.Method == HttpMethod.Post, "Register request must use POST.");
            Ensure(request.RequestUri == new Uri("http://login.test:18080/auth/register"),
                "Register request URI mismatch.");
            string payload = await request.Content!.ReadAsStringAsync(cancellationToken);
            using JsonDocument document = JsonDocument.Parse(payload);
            Ensure(document.RootElement.GetProperty("loginId").GetString() == "new-user", "Register loginId mismatch.");
            Ensure(document.RootElement.GetProperty("password").GetString() == "new-secret", "Register password mismatch.");
            Ensure(document.RootElement.GetProperty("nickname").GetString() == "New Hero", "Register nickname mismatch.");
            return JsonResponse(HttpStatusCode.Created,
                """{"success":true,"userId":77,"nickname":"New Hero"}""");
        }));
        var client = new WorldLoginApiClient(httpClient);

        WorldRegisterResponse response = await client.RegisterAsync(
            new WorldLoginServerSettings("http://login.test:18080"),
            new WorldRegisterRequest("new-user", "new-secret", "New Hero"));

        Ensure(response == new WorldRegisterResponse(77, "New Hero"), "Register response parse mismatch.");
    }

    private static async Task VerifyRegisterErrorResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler((_, _) => Task.FromResult(
            JsonResponse(HttpStatusCode.Conflict,
                """{"success":false,"code":"LOGIN_ID_ALREADY_EXISTS","message":"already exists"}"""))));
        var client = new WorldLoginApiClient(httpClient);

        try
        {
            await client.RegisterAsync(
                new WorldLoginServerSettings("http://login.test"),
                new WorldRegisterRequest("new-user", "new-secret", "New Hero"));
            throw new InvalidOperationException("Register HTTP error response was accepted.");
        }
        catch (WorldLoginApiException exception)
        {
            Ensure(exception.StatusCode == HttpStatusCode.Conflict, "Register HTTP status mapping mismatch.");
            Ensure(exception.ErrorCode == "LOGIN_ID_ALREADY_EXISTS", "Register error code mapping mismatch.");
        }
    }

    private static async Task VerifyInvalidRegisterResponseAsync()
    {
        using var httpClient = new HttpClient(new StubHttpMessageHandler((_, _) => Task.FromResult(
            JsonResponse(HttpStatusCode.Created,
                """{"success":true,"userId":0,"nickname":""}"""))));
        var client = new WorldLoginApiClient(httpClient);

        try
        {
            await client.RegisterAsync(
                new WorldLoginServerSettings("http://login.test"),
                new WorldRegisterRequest("new-user", "new-secret", "New Hero"));
            throw new InvalidOperationException("Invalid register response was accepted.");
        }
        catch (InvalidOperationException exception) when (
            exception.Message == "LoginServer register response was invalid.")
        {
        }
    }

    private static HttpResponseMessage JsonResponse(HttpStatusCode statusCode, string json) => new(statusCode)
    {
        Content = new StringContent(json, Encoding.UTF8, "application/json")
    };

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class StubHttpMessageHandler(
        Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> handler) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request,
            CancellationToken cancellationToken) => handler(request, cancellationToken);
    }
}
