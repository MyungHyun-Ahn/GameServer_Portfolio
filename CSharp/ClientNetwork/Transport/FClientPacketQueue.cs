using System.Threading.Channels;

namespace ClientNetwork.Transport;

public sealed class FClientPacketQueue
{
    private readonly Channel<FReceivedPacket> m_channel;

    public FClientPacketQueue(int capacity)
    {
        if (capacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity));
        }

        Capacity = capacity;
        m_channel = Channel.CreateBounded<FReceivedPacket>(new BoundedChannelOptions(capacity)
        {
            FullMode = BoundedChannelFullMode.Wait,
            SingleReader = false,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
    }

    public int Capacity { get; }
    public int Count => m_channel.Reader.Count;

    public bool TryDequeue(out FReceivedPacket packet) => m_channel.Reader.TryRead(out packet);

    public ValueTask<bool> WaitToReadAsync(CancellationToken cancellationToken = default)
    {
        return m_channel.Reader.WaitToReadAsync(cancellationToken);
    }

    internal bool TryEnqueue(FReceivedPacket packet) => m_channel.Writer.TryWrite(packet);

    internal void Clear()
    {
        while (m_channel.Reader.TryRead(out _))
        {
        }
    }
}
