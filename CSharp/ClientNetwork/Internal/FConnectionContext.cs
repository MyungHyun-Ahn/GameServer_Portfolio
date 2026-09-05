using System.Net.Sockets;
using System.Threading.Channels;
using ClientNetwork.Packet;
using ClientNetwork.Transport;

namespace ClientNetwork.Internal;

internal sealed class FConnectionContext
{
    private int m_stopRequested;
    private int m_stopSourceDisposed;
    private readonly TaskCompletionSource<bool> m_loopStartSource = new(
        TaskCreationOptions.RunContinuationsAsynchronously);

    public FConnectionContext(
        long generation,
        TcpClient client,
        NetworkStream stream,
        FClientConnectionOptions options)
    {
        Generation = generation;
        Client = client;
        Stream = stream;
        Options = options;
        StopSource = new CancellationTokenSource();

        var cipher = new FDefaultPacketCipher(options.PacketKey, options.EnablePacketCipher);
        FrameEncoder = new FPacketFrameEncoder(cipher);
        StreamDecoder = new FPacketStreamDecoder(
            cipher,
            options.ValidateChecksum,
            options.MaxBufferedReceiveBytes);
        SendQueue = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(options.MaxQueuedSendPackets)
        {
            FullMode = BoundedChannelFullMode.Wait,
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
    }

    public long Generation { get; }
    public TcpClient Client { get; }
    public NetworkStream Stream { get; }
    public FClientConnectionOptions Options { get; }
    public CancellationTokenSource StopSource { get; }
    public FPacketFrameEncoder FrameEncoder { get; }
    public FPacketStreamDecoder StreamDecoder { get; }
    public Channel<byte[]> SendQueue { get; }
    public Task SendLoopTask { get; set; } = Task.CompletedTask;
    public Task ReceiveLoopTask { get; set; } = Task.CompletedTask;

    public void StartLoops() => m_loopStartSource.TrySetResult(true);

    public async Task<bool> WaitForLoopStartAsync()
    {
        try
        {
            await m_loopStartSource.Task.WaitAsync(StopSource.Token).ConfigureAwait(false);
            return true;
        }
        catch (OperationCanceledException) when (StopSource.IsCancellationRequested)
        {
            return false;
        }
    }

    public bool TryRequestStop()
    {
        if (Interlocked.Exchange(ref m_stopRequested, 1) != 0)
        {
            return false;
        }

        SendQueue.Writer.TryComplete();
        StopSource.Cancel();
        Client.Dispose();
        return true;
    }

    public async Task WaitForLoopsAsync()
    {
        await Task.WhenAll(SendLoopTask, ReceiveLoopTask).ConfigureAwait(false);
        if (Interlocked.Exchange(ref m_stopSourceDisposed, 1) == 0)
        {
            StopSource.Dispose();
        }
    }
}
