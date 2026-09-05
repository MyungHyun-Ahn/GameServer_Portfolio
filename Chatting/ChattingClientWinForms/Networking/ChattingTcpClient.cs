using System.Text;
using ChattingClientWinForms.Models;
using ClientNetwork.Packet;
using ClientNetwork.Threading;
using ClientNetwork.Transport;
using Generated.Packets;
using Generated.Packets.Chatting;
using Generated.Packets.Login;

namespace ChattingClientWinForms.Networking;

internal sealed class ChattingTcpClient : IAsyncDisposable
{
    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly object m_connectionSetupLock = new();
    private readonly object m_connectAttemptLock = new();
    private readonly object m_dispatchLock = new();
    private readonly FClientSession m_session = new();
    private readonly PacketRouter m_packetRouter = new();
    private readonly FSerializedCallbackQueue m_callbackQueue = new();

    private CancellationTokenSource? m_dispatchCancellation;
    private Task? m_dispatchTask;
    private TaskCompletionSource? m_connectionSetupCompletion;
    private CancellationTokenSource? m_connectAttemptStopSource;
    private string? m_requestedDisconnectReason;
    private long m_requestedDisconnectGeneration;
    private long m_activeGeneration;
    private int m_disconnectNotified = 1;
    private int m_disposed;

    public ChattingTcpClient()
    {
        m_packetRouter.SetLoginHandler(new LoginResponseHandler(this));
        m_packetRouter.SetChattingHandler(new ChattingResponseHandler(this));
        m_session.Disconnected += HandleSessionDisconnected;
    }

    public event Action<string>? SystemMessageReceived;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<LoginResult>? LoginResultReceived;
    public event Action<IReadOnlyList<ChatRoomInfo>>? RoomListReceived;
    public event Action<RoomChangeResult>? RoomChangeResultReceived;
    public event Action<ChattingResult>? ChattingResultReceived;
    public event Action<BroadcastMessage>? BroadcastReceived;

    public bool IsConnected => m_session.IsConnected;

    public async Task ConnectAsync(ClientConnectionSettings settings, CancellationToken cancellationToken = default)
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
                bool isCurrentAttempt = IsCurrentConnectAttempt(connectAttemptStopSource);
                if (!isCurrentAttempt || connectAttemptStopSource.IsCancellationRequested)
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

    public Task SendLoginAsync(uint userId, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new LoginRq { UserId = userId }, cancellationToken);
    }

    public Task SendLoginAuthAsync(string ticket, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new LoginAuthRq { Ticket = ticket }, cancellationToken);
    }

    public Task SendRoomListAsync(CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new RoomListRq(), cancellationToken);
    }

    public Task SendRoomChangeAsync(uint targetRoomId, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new RoomChangeRq { TargetRoomId = targetRoomId }, cancellationToken);
    }

    public Task SendChattingAsync(
        uint roomId,
        ulong clientMessageId,
        ulong sentTick,
        string text,
        CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(new ChattingRq
        {
            RoomId = roomId,
            ClientMessageId = clientMessageId,
            SentTick = sentTick,
            Payload = Encoding.UTF8.GetBytes(text)
        }, cancellationToken);
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
                long generation = Interlocked.Exchange(ref m_activeGeneration, 0);
                m_requestedDisconnectReason = "Client closed.";
                Volatile.Write(ref m_requestedDisconnectGeneration, generation);
                Interlocked.Exchange(ref m_disconnectNotified, 1);

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
        }
    }

    private async Task SendPacketAsync(IContentPacket packet, CancellationToken cancellationToken)
    {
        ThrowIfDisposed();

        if (!await m_session.SendAsync(packet, cancellationToken).ConfigureAwait(false))
        {
            throw new InvalidOperationException("Not connected.");
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

    private async Task DispatchPacketsAsync(long generation, CancellationToken cancellationToken)
    {
        try
        {
            while (await m_session.WaitToReadPacketAsync(cancellationToken).ConfigureAwait(false))
            {
                while (m_session.TryDequeuePacket(out FReceivedPacket packet))
                {
                    if (cancellationToken.IsCancellationRequested ||
                        Volatile.Read(ref m_activeGeneration) != generation)
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

        EmitSystemMessage(reason);
        EmitConnectionState(false);
    }

    private void EmitSystemMessage(string message) => InvokeSafely(SystemMessageReceived, message);

    private void EmitConnectionState(bool connected) => InvokeSafely(ConnectionStateChanged, connected);

    private void EmitLoginResult(uint userId, bool success)
    {
        InvokeSafely(LoginResultReceived, new LoginResult(userId, success));
    }

    private bool EmitRoomList(RoomListRp packet)
    {
        int roomCount = packet.RoomIds.Count;
        if (packet.RoomNames.Count != roomCount ||
            packet.ParticipantCounts.Count != roomCount ||
            packet.Capacities.Count != roomCount ||
            packet.JoinableFlags.Count != roomCount)
        {
            return false;
        }

        var rooms = new List<ChatRoomInfo>(roomCount);
        for (int index = 0; index < roomCount; ++index)
        {
            rooms.Add(new ChatRoomInfo(
                packet.RoomIds[index],
                packet.RoomNames[index],
                packet.ParticipantCounts[index],
                packet.Capacities[index],
                packet.JoinableFlags[index] != 0));
        }

        InvokeSafely(RoomListReceived, rooms);
        return true;
    }

    private void EmitRoomChangeResult(RoomChangeRp packet)
    {
        var resultCode = Enum.IsDefined(typeof(RoomFlowResultCode), packet.ResultCode)
            ? (RoomFlowResultCode)packet.ResultCode
            : RoomFlowResultCode.InternalError;
        InvokeSafely(RoomChangeResultReceived, new RoomChangeResult(
            packet.PreviousRoomId,
            packet.CurrentRoomId,
            packet.Success,
            resultCode));
    }

    private void EmitChattingResult(ChattingRp packet)
    {
        InvokeSafely(ChattingResultReceived, new ChattingResult(packet.Success));
    }

    private void EmitBroadcast(Broadcast packet)
    {
        InvokeSafely(BroadcastReceived, new BroadcastMessage(
            packet.RoomId,
            packet.SenderUserId,
            packet.MessageId,
            packet.SentTick,
            packet.Payload));
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
                    // UI/application callbacks must not terminate packet dispatch.
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
            LoginRp.OpcodeValue or LoginAuthRp.OpcodeValue => "Failed to parse LoginRp.",
            RoomListRp.OpcodeValue => "Failed to parse RoomListRp.",
            RoomChangeRp.OpcodeValue => "Failed to parse RoomChangeRp.",
            ChattingRp.OpcodeValue => "Failed to parse ChattingRp.",
            Broadcast.OpcodeValue => "Failed to parse Broadcast.",
            _ => $"Unhandled opcode received: {opcode}"
        };
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref m_disposed) != 0, this);
    }

    private sealed class LoginResponseHandler(ChattingTcpClient owner) : LoginPacketHandlerBase
    {
        public override bool HandleLoginRp(LoginRp packet)
        {
            owner.EmitLoginResult(packet.UserId, packet.Success);
            return true;
        }

        public override bool HandleLoginAuthRp(LoginAuthRp packet)
        {
            owner.EmitLoginResult(packet.UserId, packet.Success);
            return true;
        }
    }

    private sealed class ChattingResponseHandler(ChattingTcpClient owner) : ChattingPacketHandlerBase
    {
        public override bool HandleRoomListRp(RoomListRp packet)
        {
            return owner.EmitRoomList(packet);
        }

        public override bool HandleRoomChangeRp(RoomChangeRp packet)
        {
            owner.EmitRoomChangeResult(packet);
            return true;
        }

        public override bool HandleChattingRp(ChattingRp packet)
        {
            owner.EmitChattingResult(packet);
            return true;
        }

        public override bool HandleBroadcast(Broadcast packet)
        {
            owner.EmitBroadcast(packet);
            return true;
        }
    }
}
