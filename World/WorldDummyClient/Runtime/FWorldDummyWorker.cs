using System.Diagnostics;
using System.Threading.Channels;
using WorldDummyClient.Configuration;
using WorldDummyClient.Metrics;

namespace WorldDummyClient.Runtime;

internal sealed class FWorldDummyWorker
{
    private readonly WorldDummySettings m_settings;
    private readonly FWorldDummyMetrics m_metrics;
    private readonly Channel<FDummyEvent> m_events = Channel.CreateUnbounded<FDummyEvent>(
        new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
    private readonly List<FVirtualWorldUser> m_users = [];
    private int m_stopRequested;

    public FWorldDummyWorker(int index, WorldDummySettings settings, FWorldDummyMetrics metrics)
    {
        Index = index;
        m_settings = settings;
        m_metrics = metrics;
    }

    public int Index { get; }

    public void AddUser(FVirtualWorldUser user) => m_users.Add(user);

    public void Enqueue(FDummyEvent dummyEvent)
    {
        if (!m_events.Writer.TryWrite(dummyEvent))
        {
            throw new InvalidOperationException($"World dummy worker {Index} event queue is closed.");
        }
    }

    public void RequestStopUsers() => Interlocked.Exchange(ref m_stopRequested, 1);

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(m_settings.FrameInterval);
        long previousTimestamp = Stopwatch.GetTimestamp();
        try
        {
            while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false))
            {
                long now = Stopwatch.GetTimestamp();
                TimeSpan elapsed = Stopwatch.GetElapsedTime(previousTimestamp, now);
                previousTimestamp = now;
                m_metrics.RecordFrame(elapsed, m_settings.FrameInterval);

                DrainEvents(now);
                if (Interlocked.Exchange(ref m_stopRequested, 0) != 0)
                {
                    foreach (FVirtualWorldUser user in m_users)
                    {
                        user.BeginStop(now);
                    }
                }

                foreach (FVirtualWorldUser user in m_users)
                {
                    try
                    {
                        user.Tick(now, elapsed);
                    }
                    catch (Exception exception)
                    {
                        Console.Error.WriteLine($"[worker {Index}] user={user.Index} tick failed: {exception}");
                        user.FailFromWorker(exception);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
    }

    private void DrainEvents(long now)
    {
        int processed = 0;
        while (processed < m_settings.EventDrainMaxCount && m_events.Reader.TryRead(out FDummyEvent dummyEvent))
        {
            dummyEvent.User.ProcessEvent(dummyEvent, now);
            processed++;
        }
    }
}
