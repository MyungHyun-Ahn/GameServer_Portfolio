using System.Diagnostics;
using WorldClientCore.Authentication;
using WorldClientCore.Models;
using WorldClientCore.Networking;
using WorldClientCore.Simulation;
using WorldDummyClient.Configuration;
using WorldDummyClient.Metrics;

namespace WorldDummyClient.Runtime;

internal sealed class FVirtualWorldUser : IAsyncDisposable
{
	private const ushort PlayerNotReadyResultCode = 12;
	private const ushort MoveSupersededResultCode = 11;
	private const int PlayerReadyRetryMilliseconds = 100;
    private static readonly (float X, float Y)[] s_directions =
    [
        (1.0f, 0.0f),
        (-1.0f, 0.0f),
        (0.0f, 1.0f),
        (0.0f, -1.0f),
        (0.70710677f, 0.70710677f),
        (0.70710677f, -0.70710677f),
        (-0.70710677f, 0.70710677f),
        (-0.70710677f, -0.70710677f)
    ];

    private readonly WorldDummySettings m_settings;
    private readonly FWorldDummyMetrics m_metrics;
    private readonly WorldLoginApiClient m_loginApiClient;
    private readonly Action<FDummyEvent> m_enqueueEvent;
    private readonly WorldTcpClient m_client = new();
    private readonly FWorldSimulationState m_simulation = new();
    private readonly Dictionary<uint, SPendingMove> m_pendingMoves = [];
    private readonly HashSet<ulong> m_monsterEntityIds = [];
    private readonly Random m_random;

    private long m_stateDeadline;
    private long m_nextActionAt;
    private long m_nextSyncAt;
    private uint m_moveSequence;
    private float m_directionX;
    private float m_directionY = 1.0f;
    private bool m_connectCounted;
    private bool m_authFailureCounted;
    private bool m_failureCounted;
    private int m_acceptedStartCount;
    private int m_acceptedStopCount;

    public FVirtualWorldUser(
        int index,
        WorldDummySettings settings,
        FWorldDummyMetrics metrics,
        WorldLoginApiClient loginApiClient,
        Action<FDummyEvent> enqueueEvent)
    {
        Index = index;
        m_settings = settings;
        m_metrics = metrics;
        m_loginApiClient = loginApiClient;
        m_enqueueEvent = enqueueEvent;
        m_random = new Random(unchecked(settings.RandomSeed + index * 7919));

        m_client.ConnectionStateChanged += connected => Enqueue(EDummyEventType.ConnectionChanged, connected);
        m_client.MapEnterResultReceived += result => Enqueue(EDummyEventType.MapEnterResult, result);
        m_client.ActorSpawnReceived += notification => Enqueue(EDummyEventType.ActorSpawn, notification);
        m_client.ActorDespawnReceived += notification => Enqueue(EDummyEventType.ActorDespawn, notification);
        m_client.MoveResultReceived += result => Enqueue(EDummyEventType.MoveResult, result);
        m_client.MoveNotificationReceived += notification => Enqueue(EDummyEventType.MoveNotification, notification);
    }

    public int Index { get; }
    public EDummyUserState State { get; private set; } = EDummyUserState.Connecting;
    public int PendingMoveCount => m_pendingMoves.Count;
    public bool ScenarioSucceeded =>
        !m_failureCounted && m_acceptedStartCount > 0 && m_acceptedStopCount > 0 && m_pendingMoves.Count == 0;
    public string ScenarioSummary =>
        $"state={State}, startOk={m_acceptedStartCount}, stopOk={m_acceptedStopCount}, pending={m_pendingMoves.Count}";

    public async Task StartConnectAsync(CancellationToken cancellationToken)
    {
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(m_settings.ResponseTimeoutMs);
        EBootstrapPhase phase = m_settings.UseLoginServerAuthentication
            ? EBootstrapPhase.Login
            : EBootstrapPhase.Connect;
        try
        {
            WorldLoginResponse? login = null;
            if (m_settings.UseLoginServerAuthentication)
            {
                login = await LoginOrRegisterAsync(timeoutSource.Token).ConfigureAwait(false);
                phase = EBootstrapPhase.Connect;
            }

            string host = login?.WorldServer.Ip ?? m_settings.WorldServerHost;
            int port = login?.WorldServer.Port ?? m_settings.WorldServerPort;
            await m_client.ConnectAsync(
                new WorldConnectionSettings(host, port, m_settings.WorldPacketKey),
                timeoutSource.Token).ConfigureAwait(false);

            if (login is not null)
            {
                phase = EBootstrapPhase.WorldAuth;
                WorldAuthResult auth = await m_client
                    .AuthenticateAsync(login.WorldTicket, timeoutSource.Token)
                    .ConfigureAwait(false);
                Enqueue(EDummyEventType.AuthenticationResult, new SAuthenticationResult(auth, login.UserId));
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (OperationCanceledException)
        {
            EnqueueBootstrapFailure(phase, $"{phase} timeout after {m_settings.ResponseTimeoutMs}ms.");
        }
        catch (Exception exception)
        {
            EnqueueBootstrapFailure(phase, exception.Message);
        }
    }

    public void ProcessEvent(FDummyEvent dummyEvent, long now)
    {
        switch (dummyEvent.Type)
        {
            case EDummyEventType.ConnectionChanged:
                ProcessConnectionChanged((bool)dummyEvent.Payload!, now);
                break;
            case EDummyEventType.ConnectFailed:
                MarkFailed((string)dummyEvent.Payload!, bootstrap: true);
                break;
            case EDummyEventType.AuthenticationResult:
                ProcessAuthenticationResult((SAuthenticationResult)dummyEvent.Payload!, now);
                break;
            case EDummyEventType.AuthenticationFailed:
                ProcessAuthenticationFailure((string)dummyEvent.Payload!);
                break;
            case EDummyEventType.MapEnterSendFailed:
                m_metrics.RecordMapEnterFailed();
                MarkFailed((string)dummyEvent.Payload!, bootstrap: false);
                break;
            case EDummyEventType.MapEnterResult:
                ProcessMapEnterResult((MapEnterResult)dummyEvent.Payload!, now);
                break;
            case EDummyEventType.ActorSpawn:
                ActorSpawnNotification spawn = (ActorSpawnNotification)dummyEvent.Payload!;
                m_simulation.Spawn(spawn);
                if (spawn.ActorKind == EWorldActorKind.Monster)
                {
                    m_monsterEntityIds.Add(spawn.EntityId);
                }
                m_metrics.RecordSpawnNotification(spawn.ActorKind);
                break;
            case EDummyEventType.ActorDespawn:
                ulong despawnEntityId = ((ActorDespawnNotification)dummyEvent.Payload!).EntityId;
                bool wasMonster = m_monsterEntityIds.Remove(despawnEntityId);
                m_simulation.Despawn(despawnEntityId);
                m_metrics.RecordDespawnNotification(wasMonster ? EWorldActorKind.Monster : EWorldActorKind.Player);
                break;
            case EDummyEventType.MoveResult:
                ProcessMoveResult((MoveResult)dummyEvent.Payload!, dummyEvent.ReceivedAt);
                break;
            case EDummyEventType.MoveNotification:
                m_simulation.ApplyMoveNotification((MoveNotification)dummyEvent.Payload!);
                m_metrics.RecordMoveNotification();
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(dummyEvent));
        }
    }

    public void Tick(long now, TimeSpan elapsed)
    {
        if (State is EDummyUserState.Failed or EDummyUserState.Stopping)
        {
            return;
        }

        if (HasTimedOutMove(now))
        {
            MarkFailed("MoveRp timeout.", bootstrap: false);
            return;
        }

        if (State == EDummyUserState.Authenticating)
        {
            if (now >= m_stateDeadline)
            {
                ProcessAuthenticationFailure("WorldAuthRp timeout.");
            }
            return;
        }

        if (State == EDummyUserState.EnteringMap)
        {
            if (now >= m_stateDeadline)
            {
                m_metrics.RecordMapEnterFailed();
                MarkFailed("MapEnterRp timeout.", bootstrap: false);
            }
			else if (now >= m_nextActionAt)
			{
				m_nextActionAt = long.MaxValue;
				_ = SendMapEnterSafeAsync();
			}
            return;
        }

        m_simulation.Advance(elapsed);
        if (State == EDummyUserState.Idle)
        {
            if (now >= m_nextActionAt)
            {
                BeginMove(now);
            }
            return;
        }

        if (State != EDummyUserState.Moving)
        {
            return;
        }

        if (now >= m_stateDeadline)
        {
            if (!SendMove(EWorldMoveState.Stop, now))
            {
                return;
            }
            State = EDummyUserState.Idle;
            m_nextActionAt = AddMilliseconds(now, NextMilliseconds(m_settings.IdleMinMs, m_settings.IdleMaxMs));
        }
        else if (now >= m_nextSyncAt)
        {
            if (!SendMove(EWorldMoveState.Sync, now))
            {
                return;
            }
            m_nextSyncAt = AddMilliseconds(now, m_settings.SyncIntervalMs);
        }
    }

    public void BeginStop(long now)
    {
        if (State == EDummyUserState.Moving)
        {
            if (!SendMove(EWorldMoveState.Stop, now))
            {
                return;
            }
        }
        if (State != EDummyUserState.Failed)
        {
            State = EDummyUserState.Stopping;
        }
    }

    public void FailFromWorker(Exception exception) =>
        MarkFailed($"Frame processing failed: {exception.Message}", bootstrap: false);

    public async ValueTask DisposeAsync()
    {
        try
        {
            if (m_client.IsConnected)
            {
                await m_client.DisconnectAsync("World dummy completed.").ConfigureAwait(false);
            }
        }
        catch
        {
        }
        await m_client.DisposeAsync().ConfigureAwait(false);
    }

    private void ProcessConnectionChanged(bool connected, long now)
    {
        if (!connected)
        {
            if (State is not EDummyUserState.Stopping and not EDummyUserState.Failed)
            {
                if (State == EDummyUserState.Authenticating)
                {
                    ProcessAuthenticationFailure("Connection closed before WorldAuth completed.");
                }
                else
                {
                    MarkFailed("Connection closed before the scenario completed.", bootstrap: true);
                }
            }
            return;
        }

        if (State != EDummyUserState.Connecting)
        {
            return;
        }

        if (!m_connectCounted)
        {
            m_connectCounted = true;
            m_metrics.RecordConnectSucceeded();
        }
        if (m_settings.UseLoginServerAuthentication)
        {
            State = EDummyUserState.Authenticating;
            m_stateDeadline = AddMilliseconds(now, m_settings.ResponseTimeoutMs);
            return;
        }

        BeginMapEnter(now);
    }

    private void ProcessAuthenticationResult(SAuthenticationResult result, long now)
    {
        if (State == EDummyUserState.Connecting)
        {
            ProcessConnectionChanged(true, now);
        }
        if (State != EDummyUserState.Authenticating)
        {
            return;
        }
        if (!result.Auth.Succeeded)
        {
            ProcessAuthenticationFailure($"WorldAuth failed. result={result.Auth.ResultCode}");
            return;
        }
        if (result.Auth.UserId == 0 || result.Auth.UserId != result.ExpectedUserId)
        {
            ProcessAuthenticationFailure(
                $"WorldAuth user mismatch. loginUser={result.ExpectedUserId}, responseUser={result.Auth.UserId}");
            return;
        }

        m_metrics.RecordAuthSucceeded();
        BeginMapEnter(now);
    }

    private void ProcessAuthenticationFailure(string reason)
    {
        if (!m_authFailureCounted)
        {
            m_authFailureCounted = true;
            m_metrics.RecordAuthFailed();
        }
        MarkFailed(reason, bootstrap: false);
    }

    private void BeginMapEnter(long now)
    {
        State = EDummyUserState.EnteringMap;
        m_stateDeadline = AddMilliseconds(now, m_settings.ResponseTimeoutMs);
        m_nextActionAt = long.MaxValue;
        _ = SendMapEnterSafeAsync();
    }

    private async Task SendMapEnterSafeAsync()
    {
        try
        {
            await m_client.SendMapEnterAsync(CreateRequestId(), m_settings.MapDataId).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            Enqueue(EDummyEventType.MapEnterSendFailed, exception.Message);
        }
    }

    private async Task<WorldLoginResponse> LoginOrRegisterAsync(CancellationToken cancellationToken)
    {
        var serverSettings = new WorldLoginServerSettings(m_settings.LoginServerBaseUrl);
        string loginId = m_settings.BuildLoginId(Index);
        var loginRequest = new WorldLoginRequest(loginId, m_settings.LoginPassword);
        try
        {
            return await m_loginApiClient
                .LoginAsync(serverSettings, loginRequest, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (WorldLoginApiException exception) when (
            string.Equals(exception.ErrorCode, "LOGIN_ID_NOT_FOUND", StringComparison.Ordinal))
        {
            try
            {
                await m_loginApiClient
                    .RegisterAsync(
                        serverSettings,
                        new WorldRegisterRequest(loginId, m_settings.LoginPassword, m_settings.BuildNickname(Index)),
                        cancellationToken)
                    .ConfigureAwait(false);
                m_metrics.RecordRegisteredUser();
            }
            catch (WorldLoginApiException registerException) when (
                string.Equals(registerException.ErrorCode, "LOGIN_ID_ALREADY_EXISTS", StringComparison.Ordinal))
            {
                // Another dummy process may have registered the same deterministic account first.
            }

            return await m_loginApiClient
                .LoginAsync(serverSettings, loginRequest, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private void EnqueueBootstrapFailure(EBootstrapPhase phase, string reason)
    {
        EDummyEventType eventType = phase is EBootstrapPhase.Login or EBootstrapPhase.WorldAuth
            ? EDummyEventType.AuthenticationFailed
            : EDummyEventType.ConnectFailed;
        Enqueue(eventType, $"{phase} failed: {reason}");
    }

    private void ProcessMapEnterResult(MapEnterResult result, long now)
    {
        if (State != EDummyUserState.EnteringMap || result.RequestId != CreateRequestId())
        {
            return;
        }

        if (!result.Succeeded)
        {
			if (result.ResultCode == PlayerNotReadyResultCode)
			{
				m_nextActionAt = AddMilliseconds(now, PlayerReadyRetryMilliseconds);
				return;
			}
            m_metrics.RecordMapEnterFailed();
            MarkFailed($"MapEnter failed. result={result.ResultCode}", bootstrap: false);
            return;
        }

        m_simulation.EnterLocal(result);
        m_metrics.RecordMapEnterSucceeded();
        State = EDummyUserState.Idle;
        m_nextActionAt = AddMilliseconds(now, NextMilliseconds(m_settings.IdleMinMs, m_settings.IdleMaxMs));
    }

    private void ProcessMoveResult(MoveResult result, long receivedAt)
    {
        if (!m_pendingMoves.Remove(result.Sequence, out SPendingMove pendingMove))
        {
            return;
        }

        bool supersededByNewerMove = result.ResultCode == MoveSupersededResultCode &&
            m_pendingMoves.Keys.Any(sequence => sequence > result.Sequence);
        m_simulation.ApplyMoveResult(result);
        m_metrics.RecordMoveResponse(new MoveResultSample(
            result.Succeeded,
            result.IsCorrected,
            supersededByNewerMove,
            Stopwatch.GetElapsedTime(pendingMove.SentAt, receivedAt)));
        if (!result.Succeeded)
        {
            if (supersededByNewerMove)
            {
                return;
            }
            MarkFailed($"MoveRp rejected. sequence={result.Sequence}, result={result.ResultCode}", bootstrap: false);
            return;
        }

        if (pendingMove.MoveState == EWorldMoveState.Start)
        {
            m_acceptedStartCount++;
        }
        else if (pendingMove.MoveState == EWorldMoveState.Stop)
        {
            m_acceptedStopCount++;
        }
    }

    private void BeginMove(long now)
    {
        (m_directionX, m_directionY) = s_directions[m_random.Next(s_directions.Length)];
        if (!SendMove(EWorldMoveState.Start, now))
        {
            return;
        }
        State = EDummyUserState.Moving;
        m_stateDeadline = AddMilliseconds(now, NextMilliseconds(m_settings.MoveMinMs, m_settings.MoveMaxMs));
        m_nextSyncAt = AddMilliseconds(now, m_settings.SyncIntervalMs);
    }

    private bool SendMove(EWorldMoveState moveState, long now)
    {
        if (!m_simulation.TryGetLocalSnapshot(out WorldActorSnapshot snapshot))
        {
            MarkFailed("Local actor is missing.", bootstrap: false);
            return false;
        }

        uint sequence = NextMoveSequence();
        if (!m_client.TrySendMove(
                sequence,
                moveState,
                snapshot.PositionX,
                snapshot.PositionY,
                m_directionX,
                m_directionY))
        {
            m_metrics.RecordSendFailure();
            MarkFailed("MoveRq could not be queued.", bootstrap: false);
            return false;
        }

        m_simulation.SetLocalInput(m_directionX, m_directionY, moveState, sequence);
        m_pendingMoves[sequence] = new SPendingMove(now, moveState);
        m_metrics.RecordMoveSent();
        return true;
    }

    private bool HasTimedOutMove(long now)
    {
        foreach (SPendingMove pendingMove in m_pendingMoves.Values)
        {
            if (Stopwatch.GetElapsedTime(pendingMove.SentAt, now).TotalMilliseconds >= m_settings.ResponseTimeoutMs)
            {
                return true;
            }
        }
        return false;
    }

    private void MarkFailed(string reason, bool bootstrap)
    {
        if (m_failureCounted)
        {
            return;
        }

        m_failureCounted = true;
        State = EDummyUserState.Failed;
        if (bootstrap && !m_connectCounted)
        {
            m_metrics.RecordConnectFailed();
        }
        m_metrics.RecordFailedUser();
        Console.Error.WriteLine($"[user {Index}] {reason}");
    }

    private void Enqueue(EDummyEventType type, object? payload) =>
        m_enqueueEvent(new FDummyEvent(this, type, payload, Stopwatch.GetTimestamp()));

    private ulong CreateRequestId() => ((ulong)(uint)(Index + 1) << 32) | m_settings.MapDataId;

    private int NextMilliseconds(int minimum, int maximum) =>
        minimum == maximum ? minimum : m_random.Next(minimum, maximum + 1);

    private uint NextMoveSequence()
    {
        m_moveSequence++;
        if (m_moveSequence == 0)
        {
            m_moveSequence = 1;
        }
        return m_moveSequence;
    }

    private static long AddMilliseconds(long timestamp, int milliseconds) =>
        timestamp + (long)(milliseconds / 1000.0 * Stopwatch.Frequency);

    private readonly record struct SPendingMove(long SentAt, EWorldMoveState MoveState);

    private readonly record struct SAuthenticationResult(WorldAuthResult Auth, ulong ExpectedUserId);

    private enum EBootstrapPhase : byte
    {
        Login,
        Connect,
        WorldAuth
    }
}
