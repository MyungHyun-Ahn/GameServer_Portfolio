using System.Collections.Concurrent;
using ClientNetwork.Packet;
using ClientNetwork.Threading;
using ClientNetwork.Transport;
using Generated.Packets;
using Generated.Packets.Map;
using Generated.Packets.World;
using WorldClientCore.Models;

namespace WorldClientCore.Networking;

public sealed class WorldTcpClient : IAsyncDisposable
{
    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly object m_connectionSetupLock = new();
    private readonly object m_connectAttemptLock = new();
    private readonly object m_dispatchLock = new();
    private readonly FClientSession m_session = new();
    private readonly PacketRouter m_packetRouter = new();
    private readonly FSerializedCallbackQueue m_callbackQueue = new();
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<WorldAuthResult>> m_pendingWorldAuth = new();
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<EquipmentMutationResult>> m_pendingEquipmentMutations = new();

    private CancellationTokenSource? m_dispatchCancellation;
    private Task? m_dispatchTask;
    private TaskCompletionSource? m_connectionSetupCompletion;
    private CancellationTokenSource? m_connectAttemptStopSource;
    private string? m_requestedDisconnectReason;
    private long m_requestedDisconnectGeneration;
    private long m_activeGeneration;
    private long m_nextRequestId;
    private int m_disconnectNotified = 1;
    private int m_disposed;

    public WorldTcpClient()
    {
        m_packetRouter.SetMapHandler(new MapResponseHandler(this));
        m_packetRouter.SetWorldHandler(new WorldResponseHandler(this));
        m_session.Disconnected += HandleSessionDisconnected;
    }

    public event Action<string>? SystemMessageReceived;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<WorldAuthResult>? WorldAuthResultReceived;
    public event Action<EquipmentMutationResult>? EquipmentMutationResultReceived;
    public event Action<MapEnterResult>? MapEnterResultReceived;
    public event Action<ActorSpawnNotification>? ActorSpawnReceived;
    public event Action<ActorDespawnNotification>? ActorDespawnReceived;
    public event Action<MoveResult>? MoveResultReceived;
    public event Action<MoveNotification>? MoveNotificationReceived;
    public event Action<BasicAttackResult>? BasicAttackResultReceived;
    public event Action<ActorAttackNotification>? ActorAttackReceived;
    public event Action<ActorDeathNotification>? ActorDeathReceived;
    public event Action<ActorRespawnNotification>? ActorRespawnReceived;

    public bool IsConnected => m_session.IsConnected;

    public async Task ConnectAsync(
        WorldConnectionSettings settings,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ThrowIfDisposed();
        CancelConnectAttempt();

        await m_lifecycleLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        TaskCompletionSource? connectionSetupCompletion = null;
        CancellationTokenSource? connectAttemptStopSource = null;
        try
        {
            ThrowIfDisposed();
            CancelConnectAttempt();
            bool wasConnected = Volatile.Read(ref m_disconnectNotified) == 0;
            Interlocked.Exchange(ref m_activeGeneration, 0);
            await StopDispatchLoopAsync().ConfigureAwait(false);
            if (wasConnected)
            {
                NotifyDisconnected("Reconnecting.");
            }

            if (m_session.State != EClientConnectionState.Disconnected)
            {
                await m_session.DisconnectAsync().ConfigureAwait(false);
            }

            Volatile.Write(ref m_requestedDisconnectGeneration, 0);
            m_requestedDisconnectReason = null;
            connectionSetupCompletion = BeginConnectionSetup();
            connectAttemptStopSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            RegisterConnectAttempt(connectAttemptStopSource);
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        try
        {
            var options = new FClientConnectionOptions(settings.Host, settings.Port)
            {
                PacketKey = settings.PacketKey
            };
            long generation = await m_session.ConnectAsync(options, connectAttemptStopSource.Token).ConfigureAwait(false);

            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
            try
            {
                ThrowIfDisposed();
                if (!IsCurrentConnectAttempt(connectAttemptStopSource) || connectAttemptStopSource.IsCancellationRequested)
                {
                    if (m_session.ConnectionGeneration == generation &&
                        m_session.State != EClientConnectionState.Disconnected)
                    {
                        await m_session.DisconnectAsync().ConfigureAwait(false);
                    }
                    throw new OperationCanceledException(connectAttemptStopSource.Token);
                }

                Volatile.Write(ref m_activeGeneration, generation);
                StartDispatchLoop(generation);
                Interlocked.Exchange(ref m_disconnectNotified, 0);
                EmitSystemMessage($"Connected to {settings.Host}:{settings.Port}.");
                EmitConnectionState(true);
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }
        finally
        {
            CompleteConnectionSetup(connectionSetupCompletion);
            CompleteConnectAttempt(connectAttemptStopSource);
            connectAttemptStopSource.Dispose();
        }
    }

    public async Task DisconnectAsync(string reason = "Disconnected.")
    {
        ThrowIfDisposed();
        CancelConnectAttempt();

        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            CancelConnectAttempt();
            long generation = Interlocked.Exchange(ref m_activeGeneration, 0);
            m_requestedDisconnectReason = reason;
            Volatile.Write(ref m_requestedDisconnectGeneration, generation);

            if (m_session.State != EClientConnectionState.Disconnected)
            {
                await m_session.DisconnectAsync().ConfigureAwait(false);
            }

            await StopDispatchLoopAsync().ConfigureAwait(false);
            NotifyDisconnected(reason);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    public async Task<WorldAuthResult> AuthenticateAsync(
        string ticket,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(ticket))
        {
            throw new ArgumentException("World ticket must not be empty.", nameof(ticket));
        }

        ThrowIfDisposed();
        ulong requestId = NextRequestId();
        var completion = new TaskCompletionSource<WorldAuthResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!m_pendingWorldAuth.TryAdd(requestId, completion))
        {
            throw new InvalidOperationException("Duplicate WorldAuth request id.");
        }

        try
        {
            await SendPacketAsync(new WorldAuthRq
            {
                RequestId = requestId,
                Ticket = ticket
            }, cancellationToken).ConfigureAwait(false);

            return await completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            m_pendingWorldAuth.TryRemove(requestId, out _);
        }
    }

    public Task SendMapEnterAsync(
        ulong requestId,
        uint mapDataId,
        CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new MapEnterRq
        {
            RequestId = requestId,
            MapDataId = mapDataId
        }, cancellationToken);
    }

    public Task<EquipmentMutationResult> EquipItemAsync(
        ulong itemInstanceId,
        ulong expectedItemVersion,
        CancellationToken cancellationToken = default)
    {
        if (itemInstanceId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(itemInstanceId), "Item instance id must be positive.");
        }
        if (expectedItemVersion == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(expectedItemVersion), "Expected item version must be positive.");
        }

        ulong requestId = NextRequestId();
        return SendEquipmentMutationAsync(
            requestId,
            new EquipItemRq
            {
                RequestId = requestId,
                ItemInstanceId = itemInstanceId,
                ExpectedItemVersion = expectedItemVersion
            },
            cancellationToken);
    }

    public Task<EquipmentMutationResult> UnequipItemAsync(
        ulong itemInstanceId,
        ulong expectedItemVersion,
        CancellationToken cancellationToken = default)
    {
        if (itemInstanceId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(itemInstanceId), "Item instance id must be positive.");
        }
        if (expectedItemVersion == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(expectedItemVersion), "Expected item version must be positive.");
        }

        ulong requestId = NextRequestId();
        return SendEquipmentMutationAsync(
            requestId,
            new UnequipItemRq
            {
                RequestId = requestId,
                ItemInstanceId = itemInstanceId,
                ExpectedItemVersion = expectedItemVersion
            },
            cancellationToken);
    }

    public bool TrySendMove(
        uint sequence,
        EWorldMoveState moveState,
        float clientPositionX,
        float clientPositionY,
        float directionX,
        float directionY)
    {
        ThrowIfDisposed();
        return m_session.TrySend(new MoveRq
        {
            Sequence = sequence,
            MoveState = (byte)moveState,
            ClientPositionX = clientPositionX,
            ClientPositionY = clientPositionY,
            DirectionX = directionX,
            DirectionY = directionY
        });
    }

    public bool TrySendBasicAttack(uint attackSequence, ulong targetEntityId)
    {
        ThrowIfDisposed();
        if (attackSequence == 0 || targetEntityId == 0)
        {
            return false;
        }

        return m_session.TrySend(new BasicAttackRq
        {
            AttackSequence = attackSequence,
            TargetEntityId = targetEntityId
        });
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref m_disposed, 1) != 0)
        {
            return;
        }

        CancelConnectAttempt();
        try
        {
            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
            try
            {
                CancelConnectAttempt();
                Interlocked.Exchange(ref m_activeGeneration, 0);
                Interlocked.Exchange(ref m_disconnectNotified, 1);
                FailPendingWorldAuth(new ObjectDisposedException(nameof(WorldTcpClient)));
                FailPendingEquipmentMutations(new ObjectDisposedException(nameof(WorldTcpClient)));
                await StopDispatchLoopAsync().ConfigureAwait(false);
                m_session.Disconnected -= HandleSessionDisconnected;
                await m_session.DisposeAsync().ConfigureAwait(false);
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }
        finally
        {
            await m_callbackQueue.DisposeAsync().ConfigureAwait(false);
            m_lifecycleLock.Dispose();
        }
    }

    private async Task SendPacketAsync(
        IContentPacket packet,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        if (!await m_session.SendAsync(packet, cancellationToken).ConfigureAwait(false))
        {
            throw new InvalidOperationException("Not connected.");
        }
    }

    private async Task<EquipmentMutationResult> SendEquipmentMutationAsync(
        ulong requestId,
        IContentPacket packet,
        CancellationToken cancellationToken)
    {
        ThrowIfDisposed();
        var completion = new TaskCompletionSource<EquipmentMutationResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!m_pendingEquipmentMutations.TryAdd(requestId, completion))
        {
            throw new InvalidOperationException("Duplicate equipment request id.");
        }

        bool requestDispatched = false;
        try
        {
            await SendPacketAsync(packet, cancellationToken).ConfigureAwait(false);
            requestDispatched = true;
            return await completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (requestDispatched)
        {
            // The Cache transaction may already have committed. Disconnect so the next
            // connection reloads the authoritative snapshot instead of keeping stale stats.
            if (IsConnected)
            {
                await DisconnectAsync("Equipment result timed out; reconnect to reload player state.").ConfigureAwait(false);
            }
            throw;
        }
        finally
        {
            m_pendingEquipmentMutations.TryRemove(requestId, out _);
        }
    }

    private void StartDispatchLoop(long generation)
    {
        var cancellation = new CancellationTokenSource();
        lock (m_dispatchLock)
        {
            m_dispatchCancellation = cancellation;
        }

        Task dispatchTask = DispatchPacketsAsync(generation, cancellation.Token);
        lock (m_dispatchLock)
        {
            if (ReferenceEquals(m_dispatchCancellation, cancellation))
            {
                m_dispatchTask = dispatchTask;
            }
        }
    }

    private async Task DispatchPacketsAsync(
        long generation,
        CancellationToken cancellationToken)
    {
        try
        {
            while (await m_session.WaitToReadPacketAsync(cancellationToken).ConfigureAwait(false))
            {
                while (m_session.TryDequeuePacket(out FReceivedPacket packet))
                {
                    if (cancellationToken.IsCancellationRequested || Volatile.Read(ref m_activeGeneration) != generation)
                    {
                        return;
                    }

                    if (packet.ConnectionGeneration != generation)
                    {
                        continue;
                    }

                    bool handled;
                    try
                    {
                        handled = m_packetRouter.DispatchPacket(packet.Opcode, packet.Body.Span);
                    }
                    catch (Exception exception)
                    {
                        EmitSystemMessage($"Packet handler failed. opcode={packet.Opcode}, error={exception.Message}");
                        continue;
                    }

                    if (!handled)
                    {
                        EmitSystemMessage(GetDispatchFailureMessage(packet.Opcode));
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            EmitSystemMessage($"Packet dispatch stopped: {exception.Message}");
        }
    }

    private async Task StopDispatchLoopAsync()
    {
        CancellationTokenSource? cancellation;
        Task? dispatchTask;
        lock (m_dispatchLock)
        {
            cancellation = m_dispatchCancellation;
            dispatchTask = m_dispatchTask;
            m_dispatchCancellation = null;
            m_dispatchTask = null;
        }

        if (cancellation is null)
        {
            return;
        }

        try
        {
            cancellation.Cancel();
            if (dispatchTask is not null)
            {
                await dispatchTask.ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            cancellation.Dispose();
        }
    }

    private void HandleSessionDisconnected(FClientDisconnectInfo disconnectInfo)
    {
        Task connectionSetupTask = GetConnectionSetupTask();
        _ = HandleSessionDisconnectedAsync(disconnectInfo, connectionSetupTask);
    }

    private async Task HandleSessionDisconnectedAsync(
        FClientDisconnectInfo disconnectInfo,
        Task connectionSetupTask)
    {
        await connectionSetupTask.ConfigureAwait(false);
        try
        {
            await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        }
        catch (ObjectDisposedException)
        {
            return;
        }

        try
        {
            if (Interlocked.CompareExchange(
                    ref m_activeGeneration,
                    0,
                    disconnectInfo.ConnectionGeneration) != disconnectInfo.ConnectionGeneration)
            {
                return;
            }

            await StopDispatchLoopAsync().ConfigureAwait(false);
            string reason = Volatile.Read(ref m_requestedDisconnectGeneration) == disconnectInfo.ConnectionGeneration &&
                !string.IsNullOrWhiteSpace(m_requestedDisconnectReason)
                    ? m_requestedDisconnectReason
                    : BuildDisconnectReason(disconnectInfo);
            NotifyDisconnected(reason);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    private TaskCompletionSource BeginConnectionSetup()
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (m_connectionSetupLock)
        {
            m_connectionSetupCompletion = completion;
        }
        return completion;
    }

    private Task GetConnectionSetupTask()
    {
        lock (m_connectionSetupLock)
        {
            return m_connectionSetupCompletion?.Task ?? Task.CompletedTask;
        }
    }

    private void CompleteConnectionSetup(TaskCompletionSource? completion)
    {
        if (completion is null)
        {
            return;
        }

        lock (m_connectionSetupLock)
        {
            if (ReferenceEquals(m_connectionSetupCompletion, completion))
            {
                m_connectionSetupCompletion = null;
            }
        }
        completion.TrySetResult();
    }

    private void NotifyDisconnected(string reason)
    {
        if (Interlocked.Exchange(ref m_disconnectNotified, 1) != 0)
        {
            return;
        }

        FailPendingWorldAuth(new IOException(reason));
        FailPendingEquipmentMutations(new IOException(reason));
        EmitSystemMessage(reason);
        EmitConnectionState(false);
    }

    private void EmitSystemMessage(string message) => InvokeSafely(SystemMessageReceived, message);

    private void EmitConnectionState(bool connected) => InvokeSafely(ConnectionStateChanged, connected);

    private bool CompleteWorldAuth(WorldAuthRp packet)
    {
        var result = new WorldAuthResult(packet.ResultCode, packet.RequestId, packet.UserId);
        if (!m_pendingWorldAuth.TryRemove(packet.RequestId, out TaskCompletionSource<WorldAuthResult>? completion))
        {
            EmitSystemMessage($"Unexpected WorldAuthRp. request={packet.RequestId}");
            return true;
        }

        completion.TrySetResult(result);
        InvokeSafely(WorldAuthResultReceived, result);
        return true;
    }

    private bool CompleteEquipmentMutation(EquipItemRp packet) => CompleteEquipmentMutation(
        new EquipmentMutationResult(
            packet.ResultCode,
            packet.RequestId,
            packet.ItemInstanceId,
            packet.ItemVersion,
            packet.Equipped,
            packet.FinalStr,
            packet.FinalDex,
            packet.FinalInt,
            packet.FinalLuk,
            packet.CurrentHp,
            packet.MaxHp,
            packet.CurrentMp,
            packet.MaxMp,
            packet.Attack,
            packet.Defense,
            packet.MoveSpeedMilli,
            packet.EquipmentVersion,
            packet.StatRevision));

    private bool CompleteEquipmentMutation(UnequipItemRp packet) => CompleteEquipmentMutation(
        new EquipmentMutationResult(
            packet.ResultCode,
            packet.RequestId,
            packet.ItemInstanceId,
            packet.ItemVersion,
            packet.Equipped,
            packet.FinalStr,
            packet.FinalDex,
            packet.FinalInt,
            packet.FinalLuk,
            packet.CurrentHp,
            packet.MaxHp,
            packet.CurrentMp,
            packet.MaxMp,
            packet.Attack,
            packet.Defense,
            packet.MoveSpeedMilli,
            packet.EquipmentVersion,
            packet.StatRevision));

    private bool CompleteEquipmentMutation(EquipmentMutationResult result)
    {
        if (!m_pendingEquipmentMutations.TryRemove(
                result.RequestId,
                out TaskCompletionSource<EquipmentMutationResult>? completion))
        {
            EmitSystemMessage($"Unexpected equipment response. request={result.RequestId}");
            return true;
        }

        completion.TrySetResult(result);
        InvokeSafely(EquipmentMutationResultReceived, result);
        return true;
    }

    private void EmitMapEnter(MapEnterRp packet)
    {
        InvokeSafely(MapEnterResultReceived, new MapEnterResult(
            packet.ResultCode,
            packet.RequestId,
            packet.MapInstanceId,
            packet.EntityId,
            packet.PositionX,
            packet.PositionY,
            packet.DirectionX,
            packet.DirectionY,
            packet.ServerTick,
            packet.CharacterId,
            packet.CharacterDataId,
            packet.Level,
            packet.Exp,
            packet.RequiredExpToNextLevel,
            packet.StrStat,
            packet.DexStat,
            packet.IntStat,
            packet.LukStat,
            packet.UnspentStatPoints,
            packet.ProgressVersion,
            packet.StatVersion,
            packet.FinalStr,
            packet.FinalDex,
            packet.FinalInt,
            packet.FinalLuk,
            packet.CurrentHp,
            packet.MaxHp,
            packet.CurrentMp,
            packet.MaxMp,
            packet.Attack,
            packet.Defense,
            packet.MoveSpeedMilli,
            packet.EquipmentVersion,
            packet.StatRevision));
    }

    private bool EmitActorSpawn(ActorSpawnNoti packet)
    {
        if (!TryParseActorKind(packet.ActorKind, out EWorldActorKind actorKind) ||
            !TryParseMoveState(packet.MoveState, out EWorldMoveState moveState))
        {
            return false;
        }

        InvokeSafely(ActorSpawnReceived, new ActorSpawnNotification(
            packet.EntityId,
            actorKind,
            packet.ActorDataId,
            packet.PositionX,
            packet.PositionY,
            packet.DirectionX,
            packet.DirectionY,
            packet.MoveSequence,
            moveState,
            packet.ServerTick,
            packet.CurrentHp,
            packet.MaxHp,
            packet.LifeRevision));
        return true;
    }

    private void EmitActorDespawn(ActorDespawnNoti packet)
    {
        InvokeSafely(ActorDespawnReceived, new ActorDespawnNotification(packet.EntityId));
    }

    private bool EmitMoveResult(MoveRp packet)
    {
        if (!TryParseMoveState(packet.MoveState, out EWorldMoveState moveState))
        {
            return false;
        }

        InvokeSafely(MoveResultReceived, new MoveResult(
            packet.ResultCode,
            packet.Sequence,
            moveState,
            packet.AcceptedPositionX,
            packet.AcceptedPositionY,
            packet.DirectionX,
            packet.DirectionY,
            packet.IsCorrected));
        return true;
    }

    private bool EmitMoveNotification(MoveNoti packet)
    {
        if (!TryParseMoveState(packet.MoveState, out EWorldMoveState moveState))
        {
            return false;
        }

        InvokeSafely(MoveNotificationReceived, new MoveNotification(
            packet.EntityId,
            packet.Sequence,
            moveState,
            packet.PositionX,
            packet.PositionY,
            packet.DirectionX,
            packet.DirectionY,
            packet.ServerTick));
        return true;
    }

    private void EmitActorAttack(ActorAttackNoti packet)
    {
        InvokeSafely(ActorAttackReceived, new ActorAttackNotification(
            packet.AttackerEntityId,
            packet.TargetEntityId,
            packet.Damage,
            packet.TargetCurrentHp,
            packet.TargetMaxHp,
            packet.ServerTick));
    }

    private void EmitBasicAttackResult(BasicAttackRp packet)
    {
        InvokeSafely(BasicAttackResultReceived, new BasicAttackResult(
            packet.ResultCode,
            packet.AttackSequence,
            packet.ServerTick));
    }

    private void EmitActorDeath(ActorDeathNoti packet)
    {
        InvokeSafely(ActorDeathReceived, new ActorDeathNotification(
            packet.EntityId,
            packet.KillerEntityId,
            packet.LifeRevision,
            packet.ServerTick));
    }

    private void EmitActorRespawn(ActorRespawnNoti packet)
    {
        InvokeSafely(ActorRespawnReceived, new ActorRespawnNotification(
            packet.EntityId,
            packet.PositionX,
            packet.PositionY,
            packet.DirectionX,
            packet.DirectionY,
            packet.CurrentHp,
            packet.MaxHp,
            packet.LifeRevision,
            packet.ServerTick));
    }

    private void InvokeSafely<T>(Action<T>? callbacks, T argument)
    {
        if (callbacks is null)
        {
            return;
        }

        Delegate[] invocationList = callbacks.GetInvocationList();
        m_callbackQueue.Enqueue(() =>
        {
            foreach (Delegate callback in invocationList)
            {
                if (Volatile.Read(ref m_disposed) != 0)
                {
                    return;
                }

                try
                {
                    ((Action<T>)callback)(argument);
                }
                catch
                {
                    // One UI/application subscriber must not terminate packet dispatch.
                }
            }
        });
    }

    private void RegisterConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            m_connectAttemptStopSource = stopSource;
        }
    }

    private bool IsCurrentConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            return ReferenceEquals(m_connectAttemptStopSource, stopSource);
        }
    }

    private void CompleteConnectAttempt(CancellationTokenSource stopSource)
    {
        lock (m_connectAttemptLock)
        {
            if (ReferenceEquals(m_connectAttemptStopSource, stopSource))
            {
                m_connectAttemptStopSource = null;
            }
        }
    }

    private void CancelConnectAttempt()
    {
        lock (m_connectAttemptLock)
        {
            try
            {
                m_connectAttemptStopSource?.Cancel();
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    private ulong NextRequestId()
    {
        while (true)
        {
            ulong requestId = unchecked((ulong)Interlocked.Increment(ref m_nextRequestId));
            if (requestId != 0)
            {
                return requestId;
            }
        }
    }

    private void FailPendingWorldAuth(Exception exception)
    {
        foreach ((ulong requestId, TaskCompletionSource<WorldAuthResult> completion) in m_pendingWorldAuth)
        {
            if (m_pendingWorldAuth.TryRemove(requestId, out _))
            {
                completion.TrySetException(exception);
            }
        }
    }

    private void FailPendingEquipmentMutations(Exception exception)
    {
        foreach ((ulong requestId, TaskCompletionSource<EquipmentMutationResult> completion) in m_pendingEquipmentMutations)
        {
            if (m_pendingEquipmentMutations.TryRemove(requestId, out _))
            {
                completion.TrySetException(exception);
            }
        }
    }

    private static bool TryParseMoveState(
        byte value,
        out EWorldMoveState moveState)
    {
        moveState = (EWorldMoveState)value;
        return Enum.IsDefined(moveState);
    }

    private static bool TryParseActorKind(
        byte value,
        out EWorldActorKind actorKind)
    {
        actorKind = (EWorldActorKind)value;
        return Enum.IsDefined(actorKind);
    }

    private static string BuildDisconnectReason(FClientDisconnectInfo disconnectInfo)
    {
        return disconnectInfo.Reason switch
        {
            EClientDisconnectReason.RemoteClosed => "Connection closed by server.",
            EClientDisconnectReason.LocalRequest => "Disconnected.",
            EClientDisconnectReason.Disposed => "Client closed.",
            _ => $"Connection lost: {disconnectInfo.Exception?.Message ?? disconnectInfo.Message}"
        };
    }

    private static string GetDispatchFailureMessage(ushort opcode)
    {
        return opcode switch
        {
            WorldAuthRp.OpcodeValue => "Failed to parse WorldAuthRp.",
            EquipItemRp.OpcodeValue => "Failed to parse EquipItemRp.",
            UnequipItemRp.OpcodeValue => "Failed to parse UnequipItemRp.",
            MapEnterRp.OpcodeValue => "Failed to parse MapEnterRp.",
            ActorSpawnNoti.OpcodeValue => "Failed to parse ActorSpawnNoti.",
            ActorDespawnNoti.OpcodeValue => "Failed to parse ActorDespawnNoti.",
            MoveRp.OpcodeValue => "Failed to parse MoveRp.",
            MoveNoti.OpcodeValue => "Failed to parse MoveNoti.",
            BasicAttackRp.OpcodeValue => "Failed to parse BasicAttackRp.",
            ActorAttackNoti.OpcodeValue => "Failed to parse ActorAttackNoti.",
            ActorDeathNoti.OpcodeValue => "Failed to parse ActorDeathNoti.",
            ActorRespawnNoti.OpcodeValue => "Failed to parse ActorRespawnNoti.",
            _ => $"Unhandled opcode received: {opcode}"
        };
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref m_disposed) != 0, this);
    }

    private sealed class MapResponseHandler(WorldTcpClient owner) : MapPacketHandlerBase
    {
        public override bool HandleMapEnterRp(MapEnterRp packet)
        {
            owner.EmitMapEnter(packet);
            return true;
        }

        public override bool HandleActorSpawnNoti(ActorSpawnNoti packet) => owner.EmitActorSpawn(packet);

        public override bool HandleActorDespawnNoti(ActorDespawnNoti packet)
        {
            owner.EmitActorDespawn(packet);
            return true;
        }

        public override bool HandleMoveRp(MoveRp packet) => owner.EmitMoveResult(packet);

        public override bool HandleMoveNoti(MoveNoti packet) => owner.EmitMoveNotification(packet);

        public override bool HandleBasicAttackRp(BasicAttackRp packet)
        {
            owner.EmitBasicAttackResult(packet);
            return true;
        }

        public override bool HandleActorAttackNoti(ActorAttackNoti packet)
        {
            owner.EmitActorAttack(packet);
            return true;
        }

        public override bool HandleActorDeathNoti(ActorDeathNoti packet)
        {
            owner.EmitActorDeath(packet);
            return true;
        }

        public override bool HandleActorRespawnNoti(ActorRespawnNoti packet)
        {
            owner.EmitActorRespawn(packet);
            return true;
        }
    }

    private sealed class WorldResponseHandler(WorldTcpClient owner) : WorldPacketHandlerBase
    {
        public override bool HandleWorldAuthRp(WorldAuthRp packet) => owner.CompleteWorldAuth(packet);

        public override bool HandleEquipItemRp(EquipItemRp packet) => owner.CompleteEquipmentMutation(packet);

        public override bool HandleUnequipItemRp(UnequipItemRp packet) => owner.CompleteEquipmentMutation(packet);
    }
}
