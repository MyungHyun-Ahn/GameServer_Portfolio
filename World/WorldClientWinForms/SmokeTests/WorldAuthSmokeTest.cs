using System.Threading.Channels;
using WorldClientCore.Authentication;
using WorldClientCore.Models;
using WorldClientCore.Networking;
using WorldClientWinForms.Configuration;

namespace WorldClientWinForms.SmokeTests;

internal static class WorldAuthSmokeTest
{
    private static readonly TimeSpan s_stepTimeout = TimeSpan.FromSeconds(15.0);

    private enum EEquipmentOperation
    {
        Equip,
        Unequip
    }

    private sealed record EquipmentAction(
        EEquipmentOperation Operation,
        ulong ItemInstanceId,
        ulong ExpectedVersion,
        ushort ExpectedResultCode);

    public static async Task<int> RunAsync(
        ClientSettings settings,
        IReadOnlyList<string> args,
        uint mapDataId)
    {
        await using var client = new WorldTcpClient();
        var mapResults = Channel.CreateUnbounded<MapEnterResult>();
        var moveResults = Channel.CreateUnbounded<MoveResult>();
        var basicAttackResults = Channel.CreateUnbounded<BasicAttackResult>();
        client.SystemMessageReceived += message => Console.WriteLine($"[WorldAuth smoke] {message}");
        client.MapEnterResultReceived += result => mapResults.Writer.TryWrite(result);
        client.MoveResultReceived += result => moveResults.Writer.TryWrite(result);
        client.BasicAttackResultReceived += result => basicAttackResults.Writer.TryWrite(result);

        try
        {
            string? suppliedTicket = GetOption(args, "--world-ticket");
            ushort expectedAuthResult = ParseUInt16Option(args, "--expect-auth-result") ?? 0;
            bool checkPreAuthRejection = HasFlag(args, "--pre-auth-check");
            bool checkRepeatedAuth = HasFlag(args, "--repeat-auth-check");
            IReadOnlyList<EquipmentAction> equipmentActions = ParseEquipmentActions(args);
            WorldLoginResponse? login = null;
            string ticket;
            string host;
            int port;

            if (!string.IsNullOrWhiteSpace(suppliedTicket))
            {
                ticket = suppliedTicket;
                host = GetOption(args, "--world-host") ?? settings.WorldServerHost;
                port = ParseInt32Option(args, "--world-port") ?? settings.WorldServerPort;
                Console.WriteLine($"[WorldAuth smoke] supplied ticket mode. world={host}:{port}");
            }
            else
            {
                string loginId = GetOption(args, "--login-id") ?? settings.LoginId;
                string password = GetOption(args, "--password") ?? settings.Password;
                if (string.IsNullOrWhiteSpace(loginId) || string.IsNullOrEmpty(password))
                {
                    throw new InvalidOperationException(
                        "--auth-smoke requires LoginId/Password in appsettings.json or --login-id/--password arguments. " +
                        "Use --world-ticket to test a ticket directly.");
                }

                var apiClient = new WorldLoginApiClient();
                using var loginTimeout = new CancellationTokenSource(s_stepTimeout);
                login = await apiClient.LoginAsync(
                    new WorldLoginServerSettings(settings.LoginServerBaseUrl),
                    new WorldLoginRequest(loginId, password),
                    loginTimeout.Token);
                ticket = login.WorldTicket;
                host = GetOption(args, "--world-host") ?? login.WorldServer.Ip;
                port = ParseInt32Option(args, "--world-port") ?? login.WorldServer.Port;
                Console.WriteLine(
                    $"[PASS] Login HTTP: user={login.UserId}, world={login.WorldServer.Ip}:{login.WorldServer.Port}, " +
                    $"instance={login.WorldServer.InstanceId}, expires={login.TicketExpiresInSeconds}s");
            }

            ValidateEndpoint(host, port);
            using var stepTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(45.0));
            await client.ConnectAsync(
                new WorldConnectionSettings(host, port, settings.WorldPacketKey),
                stepTimeout.Token);

            if (checkPreAuthRejection)
            {
                await VerifyPreAuthRejectionAsync(
                    client,
                    mapResults.Reader,
                    moveResults.Reader,
                    basicAttackResults.Reader,
                    mapDataId,
                    stepTimeout.Token);
            }

            WorldAuthResult auth = await client.AuthenticateAsync(ticket, stepTimeout.Token);
            Ensure(
                auth.ResultCode == expectedAuthResult,
                $"WorldAuth result mismatch. expected={expectedAuthResult}, actual={auth.ResultCode}");
            if (expectedAuthResult != 0)
            {
                Console.WriteLine($"[PASS] WorldAuth rejected with expected result={auth.ResultCode}.");
                await client.DisconnectAsync("Expected auth rejection verified.");
                return 0;
            }

            Ensure(auth.Succeeded && auth.UserId != 0, "WorldAuth success response has an invalid user id.");
            if (login is not null)
            {
                Ensure(auth.UserId == login.UserId, "Login HTTP and WorldAuth returned different user ids.");
            }
            Console.WriteLine($"[PASS] WorldAuth: user={auth.UserId}, request={auth.RequestId}");

            if (checkRepeatedAuth)
            {
                WorldAuthResult repeatedAuth = await client.AuthenticateAsync(ticket, stepTimeout.Token);
                Ensure(
                    repeatedAuth.ResultCode == 17,
                    $"Repeated WorldAuth must return AlreadyAuthenticated(17). actual={repeatedAuth.ResultCode}");
                Console.WriteLine("[PASS] repeated WorldAuth rejected with AlreadyAuthenticated(17).");
            }

            const ulong mapEnterRequestId = 90_001;
            await client.SendMapEnterAsync(mapEnterRequestId, mapDataId, stepTimeout.Token);
            MapEnterResult mapEnter = await ReadUntilAsync(
                mapResults.Reader,
                result => result.RequestId == mapEnterRequestId,
                "MapEnterRp",
                stepTimeout.Token);
            Ensure(mapEnter.Succeeded, $"MapEnter failed. result={mapEnter.ResultCode}");
            Console.WriteLine(
                $"[PASS] MapEnter after WorldAuth: map={mapEnter.MapInstanceId}, entity={mapEnter.EntityId}, " +
                $"stats={mapEnter.FinalStr}/{mapEnter.FinalDex}/{mapEnter.FinalIntelligence}/{mapEnter.FinalLuk}, " +
                $"hp={mapEnter.CurrentHp}/{mapEnter.MaxHp}, mp={mapEnter.CurrentMp}/{mapEnter.MaxMp}, " +
                $"attack={mapEnter.Attack}, defense={mapEnter.Defense}, moveSpeedMilli={mapEnter.MoveSpeedMilli}, " +
                $"equipmentVersion={mapEnter.EquipmentVersion}, statRevision={mapEnter.StatRevision}");

            foreach (EquipmentAction action in equipmentActions)
            {
                EquipmentMutationResult result = action.Operation == EEquipmentOperation.Equip
                    ? await client.EquipItemAsync(action.ItemInstanceId, action.ExpectedVersion, stepTimeout.Token)
                    : await client.UnequipItemAsync(action.ItemInstanceId, action.ExpectedVersion, stepTimeout.Token);
                Ensure(
                    result.ResultCode == action.ExpectedResultCode,
                    $"{action.Operation} result mismatch. item={action.ItemInstanceId}, " +
                    $"expected={action.ExpectedResultCode}, actual={result.ResultCode}");
                if (result.Succeeded)
                {
                    Ensure(result.ItemInstanceId == action.ItemInstanceId, $"{action.Operation} returned another item instance id.");
                    Ensure(result.ItemVersion > action.ExpectedVersion, $"{action.Operation} did not advance the item version.");
                    Ensure(result.EquipmentVersion != 0 && result.StatRevision != 0,
                        $"{action.Operation} returned invalid state revisions.");
                    Ensure(result.MaxHp != 0 && result.MaxMp != 0 && result.MoveSpeedMilli != 0,
                        $"{action.Operation} returned an invalid combat projection.");
                }
                Console.WriteLine(
                    $"[PASS] {action.Operation}: item={result.ItemInstanceId}, result={result.ResultCode}, " +
                    $"itemVersion={result.ItemVersion}, equipped={result.Equipped}, " +
                    $"stats={result.FinalStr}/{result.FinalDex}/{result.FinalIntelligence}/{result.FinalLuk}, " +
                    $"hp={result.CurrentHp}/{result.MaxHp}, mp={result.CurrentMp}/{result.MaxMp}, " +
                    $"attack={result.Attack}, defense={result.Defense}, moveSpeedMilli={result.MoveSpeedMilli}, " +
                    $"equipmentVersion={result.EquipmentVersion}, statRevision={result.StatRevision}");
            }

            await client.DisconnectAsync("WorldAuth smoke complete.");
            Console.WriteLine("[WorldAuth smoke] PASS");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[WorldAuth smoke] FAIL: {exception}");
            return 1;
        }
    }

    private static async Task VerifyPreAuthRejectionAsync(
        WorldTcpClient client,
        ChannelReader<MapEnterResult> mapResults,
        ChannelReader<MoveResult> moveResults,
        ChannelReader<BasicAttackResult> basicAttackResults,
        uint mapDataId,
        CancellationToken cancellationToken)
    {
        const ulong preAuthMapRequestId = 89_001;
        await client.SendMapEnterAsync(preAuthMapRequestId, mapDataId, cancellationToken);
        MapEnterResult mapResult = await ReadUntilAsync(
            mapResults,
            result => result.RequestId == preAuthMapRequestId,
            "pre-auth MapEnterRp",
            cancellationToken);
        Ensure(mapResult.ResultCode == 15, $"Pre-auth MapEnter must return AuthRequired(15). actual={mapResult.ResultCode}");

        const uint preAuthMoveSequence = 89_001;
        Ensure(
            client.TrySendMove(preAuthMoveSequence, EWorldMoveState.Start, 0.0f, 0.0f, 1.0f, 0.0f),
            "Pre-auth MoveRq could not be queued.");
        MoveResult moveResult = await ReadUntilAsync(
            moveResults,
            result => result.Sequence == preAuthMoveSequence,
            "pre-auth MoveRp",
            cancellationToken);
        Ensure(moveResult.ResultCode == 15, $"Pre-auth Move must return AuthRequired(15). actual={moveResult.ResultCode}");

        const uint preAuthAttackSequence = 89_001;
        Ensure(client.TrySendBasicAttack(preAuthAttackSequence, 1),
            "Pre-auth BasicAttackRq could not be queued.");
        BasicAttackResult basicAttackResult = await ReadUntilAsync(
            basicAttackResults,
            result => result.AttackSequence == preAuthAttackSequence,
            "pre-auth BasicAttackRp",
            cancellationToken);
        Ensure(basicAttackResult.ResultCode == 15,
            $"Pre-auth BasicAttack must return AuthRequired(15). actual={basicAttackResult.ResultCode}");
        Console.WriteLine("[PASS] pre-auth MapEnter/Move/BasicAttack rejected with AuthRequired(15).");
    }

    private static async Task<T> ReadUntilAsync<T>(
        ChannelReader<T> reader,
        Func<T, bool> predicate,
        string step,
        CancellationToken cancellationToken)
    {
        try
        {
            while (await reader.WaitToReadAsync(cancellationToken))
            {
                while (reader.TryRead(out T? value))
                {
                    if (predicate(value))
                    {
                        return value;
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"Timed out while waiting for {step}.");
        }

        throw new InvalidOperationException($"{step} stream completed unexpectedly.");
    }

    private static bool HasFlag(IReadOnlyList<string> args, string option) =>
        args.Any(argument => string.Equals(argument, option, StringComparison.OrdinalIgnoreCase));

    private static string? GetOption(IReadOnlyList<string> args, string option)
    {
        for (int index = 0; index < args.Count; ++index)
        {
            if (!string.Equals(args[index], option, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (index + 1 >= args.Count)
            {
                throw new ArgumentException($"{option} requires a value.");
            }
            return args[index + 1];
        }
        return null;
    }

    private static IReadOnlyList<EquipmentAction> ParseEquipmentActions(IReadOnlyList<string> args)
    {
        var actions = new List<EquipmentAction>();
        for (int index = 0; index < args.Count; ++index)
        {
            if (!string.Equals(args[index], "--equipment-action", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (++index >= args.Count)
            {
                throw new ArgumentException("--equipment-action requires equip|unequip:itemInstanceId:expectedVersion[:expectedResultCode].");
            }

            string[] fields = args[index].Split(':', StringSplitOptions.TrimEntries);
            if (fields.Length is < 3 or > 4 ||
                !Enum.TryParse(fields[0], ignoreCase: true, out EEquipmentOperation operation) ||
                !ulong.TryParse(fields[1], out ulong itemInstanceId) || itemInstanceId == 0 ||
                !ulong.TryParse(fields[2], out ulong expectedVersion) || expectedVersion == 0 ||
                (fields.Length == 4 && !ushort.TryParse(fields[3], out _)))
            {
                throw new ArgumentException(
                    $"Invalid --equipment-action '{args[index]}'. " +
                    "Expected equip|unequip:itemInstanceId:expectedVersion[:expectedResultCode].");
            }

            ushort expectedResultCode = fields.Length == 4 ? ushort.Parse(fields[3]) : (ushort)0;
            actions.Add(new EquipmentAction(operation, itemInstanceId, expectedVersion, expectedResultCode));
        }
        return actions;
    }

    private static ushort? ParseUInt16Option(IReadOnlyList<string> args, string option)
    {
        string? value = GetOption(args, option);
        if (value is null)
        {
            return null;
        }
        if (!ushort.TryParse(value, out ushort result))
        {
            throw new ArgumentException($"{option} requires a UInt16 value.");
        }
        return result;
    }

    private static int? ParseInt32Option(IReadOnlyList<string> args, string option)
    {
        string? value = GetOption(args, option);
        if (value is null)
        {
            return null;
        }
        if (!int.TryParse(value, out int result))
        {
            throw new ArgumentException($"{option} requires an integer value.");
        }
        return result;
    }

    private static void ValidateEndpoint(string host, int port)
    {
        if (string.IsNullOrWhiteSpace(host) || port is <= 0 or > ushort.MaxValue)
        {
            throw new InvalidOperationException($"World endpoint is invalid. host={host}, port={port}");
        }
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
