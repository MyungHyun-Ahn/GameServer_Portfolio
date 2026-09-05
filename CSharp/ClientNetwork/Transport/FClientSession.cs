using System.Buffers;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Threading.Channels;
using ClientNetwork.Internal;
using ClientNetwork.Packet;

namespace ClientNetwork.Transport;

public sealed class FClientSession : IAsyncDisposable
{
    private const int DefaultReceiveQueueCapacity = 4096;

    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly object m_pendingConnectLock = new();
    private readonly object m_receiveOwnershipLock = new();
    private readonly FClientPacketQueue m_receivedPackets;
    private readonly ConcurrentQueue<Action> m_callbackQueue = new();

    private FConnectionContext? m_context;
    private CancellationTokenSource? m_pendingConnectStopSource;
    private FClientDisconnectInfo? m_lastDisconnectInfo;
    private long m_nextGeneration;
    private int m_state = (int)EClientConnectionState.Disconnected;
    private int m_disposed;
    private int m_callbackPumpActive;

    public FClientSession(int receiveQueueCapacity = DefaultReceiveQueueCapacity)
    {
        m_receivedPackets = new FClientPacketQueue(receiveQueueCapacity);
    }

    public event Action<long>? Connected;
    public event Action<FClientDisconnectInfo>? Disconnected;

    public EClientConnectionState State => (EClientConnectionState)Volatile.Read(ref m_state);
    public bool IsConnected => State == EClientConnectionState.Connected;
    public long ConnectionGeneration => Volatile.Read(ref m_context)?.Generation ?? 0;
    public int PendingPacketCount => m_receivedPackets.Count;
    public FClientDisconnectInfo? LastDisconnectInfo => Volatile.Read(ref m_lastDisconnectInfo);

    public async Task<long> ConnectAsync(
        FClientConnectionOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        options.Validate();
        ThrowIfDisposed();

        await m_lifecycleLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        TcpClient? client = null;
        CancellationTokenSource? connectStopSource = null;
        long generation = 0;
        bool connectionAttemptStarted = false;
        try
        {
            ThrowIfDisposed();
            if (State != EClientConnectionState.Disconnected || Volatile.Read(ref m_context) is not null)
            {
                throw new InvalidOperationException("The client session is already connected or changing state.");
            }

            connectionAttemptStarted = true;
            generation = Interlocked.Increment(ref m_nextGeneration);
            connectStopSource = new CancellationTokenSource();
            RegisterPendingConnect(connectStopSource);
            Volatile.Write(ref m_state, (int)EClientConnectionState.Connecting);

            client = new TcpClient
            {
                NoDelay = options.NoDelay
            };

            ThrowIfDisposed();

            using var connectSource = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                connectStopSource.Token);
            if (options.ConnectTimeout != Timeout.InfiniteTimeSpan)
            {
                connectSource.CancelAfter(options.ConnectTimeout);
            }

            try
            {
                await client.ConnectAsync(options.Host, options.Port, connectSource.Token).ConfigureAwait(false);
                connectSource.Token.ThrowIfCancellationRequested();
                ThrowIfDisposed();
            }
            catch (OperationCanceledException) when (connectStopSource.IsCancellationRequested)
            {
                throw;
            }
            catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"Connecting to {options.Host}:{options.Port} exceeded {options.ConnectTimeout}.",
                    exception);
            }

            NetworkStream stream = client.GetStream();
            var context = new FConnectionContext(generation, client, stream, options);
            lock (m_receiveOwnershipLock)
            {
                m_receivedPackets.Clear();
                Volatile.Write(ref m_context, context);
            }

            Volatile.Write(ref m_lastDisconnectInfo, null);
            Volatile.Write(ref m_state, (int)EClientConnectionState.Connected);

            EnqueueCallbacks(Connected, generation);
            context.SendLoopTask = RunSendLoopAsync(context);
            context.ReceiveLoopTask = RunReceiveLoopAsync(context);
            _ = context.WaitForLoopsAsync();
            StartCallbackPump();
            context.StartLoops();
            client = null;
            connectionAttemptStarted = false;
        }
        catch (Exception exception) when (connectionAttemptStarted)
        {
            client?.Dispose();
            Volatile.Write(ref m_state, (int)EClientConnectionState.Disconnected);
            Volatile.Write(ref m_lastDisconnectInfo, new FClientDisconnectInfo(
                generation,
                EClientDisconnectReason.ConnectFailed,
                "The connection attempt failed.",
                exception));
            throw;
        }
        finally
        {
            try
            {
                if (connectStopSource is not null)
                {
                    UnregisterPendingConnect(connectStopSource);
                    connectStopSource.Dispose();
                }
            }
            finally
            {
                m_lifecycleLock.Release();
            }
        }

        return generation;
    }

    public bool TrySend(IContentPacket packet)
    {
        ArgumentNullException.ThrowIfNull(packet);
        ThrowIfDisposed();

        FConnectionContext? context = Volatile.Read(ref m_context);
        if (context is null || State != EClientConnectionState.Connected)
        {
            return false;
        }

        byte[] frame = context.FrameEncoder.Encode(packet);
        return ReferenceEquals(Volatile.Read(ref m_context), context) &&
            context.SendQueue.Writer.TryWrite(frame);
    }

    public async ValueTask<bool> SendAsync(
        IContentPacket packet,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(packet);
        ThrowIfDisposed();

        FConnectionContext? context = Volatile.Read(ref m_context);
        if (context is null || State != EClientConnectionState.Connected)
        {
            return false;
        }

        byte[] frame = context.FrameEncoder.Encode(packet);
        try
        {
            await context.SendQueue.Writer.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
            return ReferenceEquals(Volatile.Read(ref m_context), context);
        }
        catch (ChannelClosedException)
        {
            return false;
        }
    }

    public bool TryDequeuePacket(out FReceivedPacket packet) => m_receivedPackets.TryDequeue(out packet);

    public ValueTask<bool> WaitToReadPacketAsync(CancellationToken cancellationToken = default)
    {
        return m_receivedPackets.WaitToReadAsync(cancellationToken);
    }

    public Task DisconnectAsync()
    {
        ThrowIfDisposed();
        CancelPendingConnect();
        return DisconnectCoreAsync(EClientDisconnectReason.LocalRequest);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref m_disposed, 1) != 0)
        {
            return;
        }

        CancelPendingConnect();
        await DisconnectCoreAsync(EClientDisconnectReason.Disposed).ConfigureAwait(false);
        m_lifecycleLock.Dispose();
    }

    private async Task DisconnectCoreAsync(EClientDisconnectReason reason)
    {
        FConnectionContext? context = null;
        FClientDisconnectInfo? disconnectInfo = null;

        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            lock (m_receiveOwnershipLock)
            {
                context = Interlocked.Exchange(ref m_context, null);
            }

            if (context is null)
            {
                Volatile.Write(ref m_state, (int)EClientConnectionState.Disconnected);
                return;
            }

            Volatile.Write(ref m_state, (int)EClientConnectionState.Disconnecting);
            context.TryRequestStop();
            disconnectInfo = new FClientDisconnectInfo(
                context.Generation,
                reason,
                reason == EClientDisconnectReason.Disposed
                    ? "The client session was disposed."
                    : "The connection was closed by the client.");
            Volatile.Write(ref m_lastDisconnectInfo, disconnectInfo);
            Volatile.Write(ref m_state, (int)EClientConnectionState.Disconnected);
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        if (disconnectInfo is not null)
        {
            QueueCallbacks(Disconnected, disconnectInfo);
        }

        if (context is not null)
        {
            await context.WaitForLoopsAsync().ConfigureAwait(false);
        }
    }

    private async Task RunSendLoopAsync(FConnectionContext context)
    {
        FClientDisconnectInfo? failure = null;
        try
        {
            if (!await context.WaitForLoopStartAsync().ConfigureAwait(false))
            {
                return;
            }

            ChannelReader<byte[]> reader = context.SendQueue.Reader;
            while (await reader.WaitToReadAsync(context.StopSource.Token).ConfigureAwait(false))
            {
                while (reader.TryRead(out byte[]? frame))
                {
                    await context.Stream.WriteAsync(frame, context.StopSource.Token).ConfigureAwait(false);
                }
            }
        }
        catch (OperationCanceledException) when (context.StopSource.IsCancellationRequested)
        {
        }
        catch (ObjectDisposedException) when (context.StopSource.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            failure = new FClientDisconnectInfo(
                context.Generation,
                EClientDisconnectReason.SendFailed,
                "The send loop failed.",
                exception);
        }

        if (failure is not null)
        {
            ReportWorkerDisconnect(context, failure);
        }
    }

    private async Task RunReceiveLoopAsync(FConnectionContext context)
    {
        byte[]? receiveBuffer = null;
        FClientDisconnectInfo? failure = null;
        try
        {
            if (!await context.WaitForLoopStartAsync().ConfigureAwait(false))
            {
                return;
            }

            receiveBuffer = ArrayPool<byte>.Shared.Rent(context.Options.ReceiveChunkSize);
            while (!context.StopSource.IsCancellationRequested)
            {
                int receivedByteCount = await context.Stream
                    .ReadAsync(receiveBuffer.AsMemory(0, context.Options.ReceiveChunkSize), context.StopSource.Token)
                    .ConfigureAwait(false);
                if (receivedByteCount == 0)
                {
                    failure = new FClientDisconnectInfo(
                        context.Generation,
                        EClientDisconnectReason.RemoteClosed,
                        "The remote endpoint closed the connection.");
                    break;
                }

                if (!context.StreamDecoder.TryAppend(receiveBuffer.AsSpan(0, receivedByteCount)))
                {
                    failure = CreateInvalidFrameInfo(context);
                    break;
                }

                while (true)
                {
                    EFrameReadStatus status = context.StreamDecoder.TryReadFrame(
                        out FDecodedContentFrame? frame,
                        out EFrameDecodeError error);
                    if (status == EFrameReadStatus.NeedMoreData)
                    {
                        break;
                    }

                    if (status == EFrameReadStatus.InvalidData || frame is null)
                    {
                        failure = new FClientDisconnectInfo(
                            context.Generation,
                            EClientDisconnectReason.InvalidFrame,
                            $"The received frame is invalid: {error}.");
                        break;
                    }

                    lock (m_receiveOwnershipLock)
                    {
                        if (!ReferenceEquals(Volatile.Read(ref m_context), context))
                        {
                            return;
                        }

                        if (!m_receivedPackets.TryEnqueue(new FReceivedPacket(
                            context.Generation,
                            frame.Opcode,
                            frame.Body)))
                        {
                            failure = new FClientDisconnectInfo(
                                context.Generation,
                                EClientDisconnectReason.ReceiveQueueOverflow,
                                $"The receive queue reached its {m_receivedPackets.Capacity}-packet limit.");
                            break;
                        }
                    }
                }

                if (failure is not null)
                {
                    break;
                }
            }
        }
        catch (OperationCanceledException) when (context.StopSource.IsCancellationRequested)
        {
        }
        catch (ObjectDisposedException) when (context.StopSource.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            failure = new FClientDisconnectInfo(
                context.Generation,
                EClientDisconnectReason.ReceiveFailed,
                "The receive loop failed.",
                exception);
        }
        finally
        {
            if (receiveBuffer is not null)
            {
                ArrayPool<byte>.Shared.Return(receiveBuffer);
            }
        }

        if (failure is not null)
        {
            ReportWorkerDisconnect(context, failure);
        }
    }

    private static FClientDisconnectInfo CreateInvalidFrameInfo(FConnectionContext context)
    {
        return new FClientDisconnectInfo(
            context.Generation,
            EClientDisconnectReason.InvalidFrame,
            $"The receive buffer rejected data: {context.StreamDecoder.LastError}.");
    }

    private void ReportWorkerDisconnect(
        FConnectionContext context,
        FClientDisconnectInfo disconnectInfo)
    {
        FConnectionContext? original;
        lock (m_receiveOwnershipLock)
        {
            original = Interlocked.CompareExchange(ref m_context, null, context);
            if (ReferenceEquals(original, context))
            {
                Volatile.Write(ref m_lastDisconnectInfo, disconnectInfo);
                Volatile.Write(ref m_state, (int)EClientConnectionState.Disconnected);
            }
        }

        if (!ReferenceEquals(original, context))
        {
            context.TryRequestStop();
            return;
        }

        context.TryRequestStop();
        QueueCallbacks(Disconnected, disconnectInfo);
    }

    private void QueueCallbacks<T>(Action<T>? callbacks, T argument)
    {
        EnqueueCallbacks(callbacks, argument);
        StartCallbackPump();
    }

    private void EnqueueCallbacks<T>(Action<T>? callbacks, T argument)
    {
        if (callbacks is null)
        {
            return;
        }

        m_callbackQueue.Enqueue(() => InvokeCallbacks(callbacks, argument));
    }

    private void StartCallbackPump()
    {
        if (m_callbackQueue.IsEmpty || Interlocked.CompareExchange(ref m_callbackPumpActive, 1, 0) != 0)
        {
            return;
        }

        _ = Task.Run(DrainCallbackQueue);
    }

    private void DrainCallbackQueue()
    {
        while (true)
        {
            while (m_callbackQueue.TryDequeue(out Action? callback))
            {
                callback();
            }

            Interlocked.Exchange(ref m_callbackPumpActive, 0);
            if (m_callbackQueue.IsEmpty || Interlocked.CompareExchange(ref m_callbackPumpActive, 1, 0) != 0)
            {
                return;
            }
        }
    }

    private static void InvokeCallbacks<T>(Action<T> callbacks, T argument)
    {
        foreach (Delegate callback in callbacks.GetInvocationList())
        {
            try
            {
                ((Action<T>)callback)(argument);
            }
            catch
            {
                // Application callbacks must not terminate the callback pump.
            }
        }
    }

    private void RegisterPendingConnect(CancellationTokenSource stopSource)
    {
        lock (m_pendingConnectLock)
        {
            m_pendingConnectStopSource = stopSource;
        }
    }

    private void UnregisterPendingConnect(CancellationTokenSource stopSource)
    {
        lock (m_pendingConnectLock)
        {
            if (ReferenceEquals(m_pendingConnectStopSource, stopSource))
            {
                m_pendingConnectStopSource = null;
            }
        }
    }

    private void CancelPendingConnect()
    {
        lock (m_pendingConnectLock)
        {
            m_pendingConnectStopSource?.Cancel();
        }
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref m_disposed) != 0, this);
    }
}
