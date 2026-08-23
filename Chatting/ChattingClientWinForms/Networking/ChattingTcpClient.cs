using System.Net.Sockets;
using System.Text;
using ChattingClientWinForms.Models;

namespace ChattingClientWinForms.Networking;

internal sealed class ChattingTcpClient : IAsyncDisposable
{
    private readonly SemaphoreSlim m_lifecycleLock = new(1, 1);
    private readonly SemaphoreSlim m_sendLock = new(1, 1);
    private readonly List<byte> m_receiveBuffer = [];

    private TcpClient? m_tcpClient;
    private NetworkStream? m_stream;
    private CancellationTokenSource? m_receiveCancellation;
    private Task? m_receiveTask;
    private byte m_packetKey;
    private int m_disconnectNotified;
    private bool m_disposed;

    public event Action<string>? SystemMessageReceived;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<LoginResult>? LoginResultReceived;
    public event Action<IReadOnlyList<ChatRoomInfo>>? RoomListReceived;
    public event Action<RoomChangeResult>? RoomChangeResultReceived;
    public event Action<ChattingResult>? ChattingResultReceived;
    public event Action<BroadcastMessage>? BroadcastReceived;

    public bool IsConnected => m_stream is not null && m_tcpClient is not null;

    public async Task ConnectAsync(ClientConnectionSettings settings, CancellationToken cancellationToken = default)
    {
        ThrowIfDisposed();

        await m_lifecycleLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await DisconnectCoreAsync("Disconnected.", notifyDisconnection: false, awaitReceiveTask: false).ConfigureAwait(false);

            TcpClient tcpClient = new();
            tcpClient.NoDelay = true;
            try
            {
                await tcpClient.ConnectAsync(settings.Host, settings.Port, cancellationToken).ConfigureAwait(false);
            }
            catch
            {
                tcpClient.Dispose();
                throw;
            }

            m_packetKey = settings.PacketKey;
            m_tcpClient = tcpClient;
            m_stream = tcpClient.GetStream();
            m_receiveBuffer.Clear();
            Interlocked.Exchange(ref m_disconnectNotified, 0);

            m_receiveCancellation = new CancellationTokenSource();
            m_receiveTask = Task.Run(() => ReceiveLoopAsync(m_stream, m_receiveCancellation.Token), CancellationToken.None);
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        EmitSystemMessage($"Connected to {settings.Host}:{settings.Port}.");
        ConnectionStateChanged?.Invoke(true);
    }

    public async Task DisconnectAsync(string reason = "Disconnected.")
    {
        ThrowIfDisposed();

        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            await DisconnectCoreAsync(reason, notifyDisconnection: true, awaitReceiveTask: false).ConfigureAwait(false);
        }
        finally
        {
            m_lifecycleLock.Release();
        }
    }

    public Task SendLoginAsync(uint userId, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(ChattingPacketCodec.CreateLoginRequestPacket(userId, m_packetKey), cancellationToken);
    }

    public Task SendLoginAuthAsync(string ticket, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(ChattingPacketCodec.CreateLoginAuthRequestPacket(ticket, m_packetKey), cancellationToken);
    }

    public Task SendRoomListAsync(CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(ChattingPacketCodec.CreateRoomListRequestPacket(m_packetKey), cancellationToken);
    }

    public Task SendRoomChangeAsync(uint targetRoomId, CancellationToken cancellationToken = default)
    {
        return SendPacketAsync(ChattingPacketCodec.CreateRoomChangeRequestPacket(targetRoomId, m_packetKey), cancellationToken);
    }

    public Task SendChattingAsync(
        uint roomId,
        ulong clientMessageId,
        ulong sentTick,
        string text,
        CancellationToken cancellationToken = default)
    {
        byte[] payload = Encoding.UTF8.GetBytes(text);
        return SendPacketAsync(
            ChattingPacketCodec.CreateChattingRequestPacket(
                roomId,
                clientMessageId,
                sentTick,
                payload,
                m_packetKey),
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (m_disposed)
        {
            return;
        }

        m_disposed = true;
        try
        {
            await DisconnectAsync("Client closed.").ConfigureAwait(false);
        }
        catch
        {
        }

        m_sendLock.Dispose();
        m_lifecycleLock.Dispose();
    }

    private async Task SendPacketAsync(byte[] packet, CancellationToken cancellationToken)
    {
        ThrowIfDisposed();

        await m_sendLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            NetworkStream? stream = m_stream;
            if (stream is null)
            {
                throw new InvalidOperationException("Not connected.");
            }

            await stream.WriteAsync(packet, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or ObjectDisposedException or SocketException)
        {
            EmitSystemMessage($"Send failed: {exception.Message}");
            throw;
        }
        finally
        {
            m_sendLock.Release();
        }
    }

    private async Task ReceiveLoopAsync(NetworkStream stream, CancellationToken cancellationToken)
    {
        string disconnectReason = "Connection closed by server.";

        try
        {
            byte[] readBuffer = new byte[4096];
            while (!cancellationToken.IsCancellationRequested)
            {
                int receivedBytes = await stream.ReadAsync(readBuffer, cancellationToken).ConfigureAwait(false);
                if (receivedBytes == 0)
                {
                    break;
                }

                AppendReceivedBytes(readBuffer.AsSpan(0, receivedBytes));
                while (true)
                {
                    if (!ChattingPacketCodec.TryExtractPacket(
                            m_receiveBuffer,
                            m_packetKey,
                            out DecodedPacket? decodedPacket,
                            out string? errorMessage))
                    {
                        if (!string.IsNullOrEmpty(errorMessage))
                        {
                            throw new InvalidOperationException(errorMessage);
                        }

                        break;
                    }

                    if (decodedPacket is not null)
                    {
                        DispatchPacket(decodedPacket);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            disconnectReason = "Disconnected.";
        }
        catch (Exception exception)
        {
            disconnectReason = $"Connection lost: {exception.Message}";
            EmitSystemMessage(disconnectReason);
        }
        finally
        {
            await CompleteReceiveLoopAsync(stream, disconnectReason).ConfigureAwait(false);
        }
    }

    private void DispatchPacket(DecodedPacket decodedPacket)
    {
        switch (decodedPacket.Opcode)
        {
        case ChattingPacketCodec.LoginRpOpcode:
        case ChattingPacketCodec.LoginAuthRpOpcode:
            if (ChattingPacketCodec.TryReadLoginResult(decodedPacket, out LoginResult loginResult))
            {
                LoginResultReceived?.Invoke(loginResult);
            }
            else
            {
                EmitSystemMessage("Failed to parse LoginRp.");
            }

            return;
        case ChattingPacketCodec.RoomListRpOpcode:
            if (ChattingPacketCodec.TryReadRoomListResult(decodedPacket, out IReadOnlyList<ChatRoomInfo> rooms))
            {
                RoomListReceived?.Invoke(rooms);
            }
            else
            {
                EmitSystemMessage("Failed to parse RoomListRp.");
            }

            return;
        case ChattingPacketCodec.RoomChangeRpOpcode:
            if (ChattingPacketCodec.TryReadRoomChangeResult(decodedPacket, out RoomChangeResult roomChangeResult))
            {
                RoomChangeResultReceived?.Invoke(roomChangeResult);
            }
            else
            {
                EmitSystemMessage("Failed to parse RoomChangeRp.");
            }

            return;
        case ChattingPacketCodec.ChattingRpOpcode:
            if (ChattingPacketCodec.TryReadChattingResult(decodedPacket, out ChattingResult chattingResult))
            {
                ChattingResultReceived?.Invoke(chattingResult);
            }
            else
            {
                EmitSystemMessage("Failed to parse ChattingRp.");
            }

            return;
        case ChattingPacketCodec.BroadcastOpcode:
            if (ChattingPacketCodec.TryReadBroadcast(decodedPacket, out BroadcastMessage broadcastMessage))
            {
                BroadcastReceived?.Invoke(broadcastMessage);
            }
            else
            {
                EmitSystemMessage("Failed to parse Broadcast.");
            }

            return;
        default:
            EmitSystemMessage($"Unhandled opcode received: {decodedPacket.Opcode}");
            return;
        }
    }

    private async Task CompleteReceiveLoopAsync(NetworkStream stream, string disconnectReason)
    {
        await m_lifecycleLock.WaitAsync().ConfigureAwait(false);
        try
        {
            if (!ReferenceEquals(m_stream, stream))
            {
                return;
            }

            CancellationTokenSource? receiveCancellation = m_receiveCancellation;
            TcpClient? tcpClient = m_tcpClient;

            m_receiveCancellation = null;
            m_receiveTask = null;
            m_stream = null;
            m_tcpClient = null;
            m_receiveBuffer.Clear();

            try
            {
                receiveCancellation?.Dispose();
            }
            catch
            {
            }

            try
            {
                stream.Dispose();
            }
            catch
            {
            }

            try
            {
                tcpClient?.Dispose();
            }
            catch
            {
            }
        }
        finally
        {
            m_lifecycleLock.Release();
        }

        NotifyDisconnected(disconnectReason);
    }

    private async Task DisconnectCoreAsync(
        string reason,
        bool notifyDisconnection,
        bool awaitReceiveTask)
    {
        CancellationTokenSource? receiveCancellation = m_receiveCancellation;
        Task? receiveTask = m_receiveTask;
        NetworkStream? stream = m_stream;
        TcpClient? tcpClient = m_tcpClient;

        m_receiveCancellation = null;
        m_receiveTask = null;
        m_stream = null;
        m_tcpClient = null;
        m_receiveBuffer.Clear();

        try
        {
            receiveCancellation?.Cancel();
        }
        catch
        {
        }

        try
        {
            stream?.Dispose();
        }
        catch
        {
        }

        try
        {
            tcpClient?.Dispose();
        }
        catch
        {
        }

        if (awaitReceiveTask && receiveTask is not null)
        {
            try
            {
                await receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
            catch (IOException)
            {
            }
        }

        try
        {
            receiveCancellation?.Dispose();
        }
        catch
        {
        }

        if (notifyDisconnection)
        {
            NotifyDisconnected(reason);
        }
    }

    private void AppendReceivedBytes(ReadOnlySpan<byte> bytes)
    {
        for (int index = 0; index < bytes.Length; ++index)
        {
            m_receiveBuffer.Add(bytes[index]);
        }
    }

    private void NotifyDisconnected(string reason)
    {
        if (Interlocked.Exchange(ref m_disconnectNotified, 1) != 0)
        {
            return;
        }

        EmitSystemMessage(reason);
        ConnectionStateChanged?.Invoke(false);
    }

    private void EmitSystemMessage(string message)
    {
        SystemMessageReceived?.Invoke(message);
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(m_disposed, this);
    }
}
