using ClientNetwork.Packet;

namespace ClientNetwork.Transport;

public sealed class FClientConnectionOptions
{
    public FClientConnectionOptions(
        string host,
        int port)
    {
        Host = host;
        Port = port;
    }

    public string Host { get; }
    public int Port { get; }
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(10);
    public bool NoDelay { get; init; } = true;
    public bool EnablePacketCipher { get; init; } = true;
    public bool ValidateChecksum { get; init; } = true;
    public byte PacketKey { get; init; }
    public int ReceiveChunkSize { get; init; } = FPacketProtocol.MaxFrameSize;
    public int MaxBufferedReceiveBytes { get; init; } = FPacketProtocol.DefaultMaxBufferedBytes;
    public int MaxQueuedSendPackets { get; init; } = 1024;

    internal void Validate()
    {
        if (string.IsNullOrWhiteSpace(Host))
        {
            throw new ArgumentException("A server host is required.", nameof(Host));
        }

        if (Port is <= 0 or > ushort.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(Port));
        }

        if (ConnectTimeout <= TimeSpan.Zero && ConnectTimeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(ConnectTimeout));
        }

        if (ReceiveChunkSize <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(ReceiveChunkSize));
        }

        if (MaxBufferedReceiveBytes < FPacketProtocol.MaxFrameSize)
        {
            throw new ArgumentOutOfRangeException(nameof(MaxBufferedReceiveBytes));
        }

        if (ReceiveChunkSize > MaxBufferedReceiveBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(ReceiveChunkSize),
                "The receive chunk cannot exceed the maximum buffered byte count.");
        }

        if (MaxQueuedSendPackets <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(MaxQueuedSendPackets));
        }
    }
}
