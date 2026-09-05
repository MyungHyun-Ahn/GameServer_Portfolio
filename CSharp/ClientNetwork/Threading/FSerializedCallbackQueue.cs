using System.Threading.Channels;

namespace ClientNetwork.Threading;

public sealed class FSerializedCallbackQueue : IAsyncDisposable
{
    [ThreadStatic]
    private static FSerializedCallbackQueue? t_executingQueue;

    private readonly Channel<Action> m_callbacks;
    private readonly Task m_pumpTask;
    private int m_disposeStarted;

    public FSerializedCallbackQueue()
    {
        m_callbacks = Channel.CreateUnbounded<Action>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
        m_pumpTask = Task.Run(DrainAsync);
    }

    public void Enqueue(Action callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        m_callbacks.Writer.TryWrite(callback);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref m_disposeStarted, 1) == 0)
        {
            m_callbacks.Writer.TryComplete();
        }

        // A synchronous subscriber may dispose its owner from inside the callback.
        // Waiting here would wait on the currently executing callback itself.
        if (ReferenceEquals(t_executingQueue, this))
        {
            return;
        }

        await m_pumpTask.ConfigureAwait(false);
    }

    private async Task DrainAsync()
    {
        await foreach (Action callback in m_callbacks.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            FSerializedCallbackQueue? previousQueue = t_executingQueue;
            t_executingQueue = this;
            try
            {
                callback();
            }
            catch
            {
                // An application callback must not terminate the shared pump.
            }
            finally
            {
                t_executingQueue = previousQueue;
            }
        }
    }
}
