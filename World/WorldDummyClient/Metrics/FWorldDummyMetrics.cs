namespace WorldDummyClient.Metrics;

using WorldClientCore.Models;

internal sealed class FWorldDummyMetrics
{
    private readonly FLatencyHistogram m_moveResponseLatency = new();
    private long m_connectSucceeded;
    private long m_connectFailed;
    private long m_registeredUsers;
    private long m_authSucceeded;
    private long m_authFailed;
    private long m_mapEnterSucceeded;
    private long m_mapEnterFailed;
    private long m_moveSent;
    private long m_moveResponses;
    private long m_moveRejected;
    private long m_moveSuperseded;
    private long m_moveCorrected;
    private long m_moveNotifications;
    private long m_playerSpawnNotifications;
    private long m_playerDespawnNotifications;
    private long m_monsterSpawnNotifications;
    private long m_monsterDespawnNotifications;
    private long m_sendFailures;
    private long m_failedUsers;
    private long m_frameCount;
    private long m_frameOverruns;
    private long m_maxFrameTicks;

    public long ConnectSucceeded => Volatile.Read(ref m_connectSucceeded);
    public long ConnectFailed => Volatile.Read(ref m_connectFailed);
    public long RegisteredUsers => Volatile.Read(ref m_registeredUsers);
    public long AuthSucceeded => Volatile.Read(ref m_authSucceeded);
    public long AuthFailed => Volatile.Read(ref m_authFailed);
    public long MapEnterSucceeded => Volatile.Read(ref m_mapEnterSucceeded);
    public long MapEnterFailed => Volatile.Read(ref m_mapEnterFailed);
    public long MoveSent => Volatile.Read(ref m_moveSent);
    public long MoveResponses => Volatile.Read(ref m_moveResponses);
    public long MoveRejected => Volatile.Read(ref m_moveRejected);
    public long MoveSuperseded => Volatile.Read(ref m_moveSuperseded);
    public long MoveNotifications => Volatile.Read(ref m_moveNotifications);
    public long PlayerSpawnNotifications => Volatile.Read(ref m_playerSpawnNotifications);
    public long MonsterSpawnNotifications => Volatile.Read(ref m_monsterSpawnNotifications);
    public long SendFailures => Volatile.Read(ref m_sendFailures);
    public long FailedUsers => Volatile.Read(ref m_failedUsers);

    public void RecordConnectSucceeded() => Interlocked.Increment(ref m_connectSucceeded);
    public void RecordConnectFailed() => Interlocked.Increment(ref m_connectFailed);
    public void RecordRegisteredUser() => Interlocked.Increment(ref m_registeredUsers);
    public void RecordAuthSucceeded() => Interlocked.Increment(ref m_authSucceeded);
    public void RecordAuthFailed() => Interlocked.Increment(ref m_authFailed);
    public void RecordMapEnterSucceeded() => Interlocked.Increment(ref m_mapEnterSucceeded);
    public void RecordMapEnterFailed() => Interlocked.Increment(ref m_mapEnterFailed);
    public void RecordMoveSent() => Interlocked.Increment(ref m_moveSent);
    public void RecordSendFailure() => Interlocked.Increment(ref m_sendFailures);
    public void RecordMoveNotification() => Interlocked.Increment(ref m_moveNotifications);
    public void RecordSpawnNotification(EWorldActorKind actorKind)
    {
        if (actorKind == EWorldActorKind.Monster)
        {
            Interlocked.Increment(ref m_monsterSpawnNotifications);
        }
        else
        {
            Interlocked.Increment(ref m_playerSpawnNotifications);
        }
    }

    public void RecordDespawnNotification(EWorldActorKind actorKind)
    {
        if (actorKind == EWorldActorKind.Monster)
        {
            Interlocked.Increment(ref m_monsterDespawnNotifications);
        }
        else
        {
            Interlocked.Increment(ref m_playerDespawnNotifications);
        }
    }
    public void RecordFailedUser() => Interlocked.Increment(ref m_failedUsers);

    public void RecordMoveResponse(MoveResultSample sample)
    {
        Interlocked.Increment(ref m_moveResponses);
        if (sample.Superseded)
        {
            Interlocked.Increment(ref m_moveSuperseded);
        }
        else if (!sample.Succeeded)
        {
            Interlocked.Increment(ref m_moveRejected);
        }
        if (sample.Corrected)
        {
            Interlocked.Increment(ref m_moveCorrected);
        }
        m_moveResponseLatency.Record(sample.Latency);
    }

    public void RecordFrame(TimeSpan elapsed, TimeSpan targetInterval)
    {
        Interlocked.Increment(ref m_frameCount);
        if (elapsed > targetInterval + targetInterval)
        {
            Interlocked.Increment(ref m_frameOverruns);
        }

        long ticks = elapsed.Ticks;
        long currentMax = Volatile.Read(ref m_maxFrameTicks);
        while (ticks > currentMax)
        {
            long observed = Interlocked.CompareExchange(ref m_maxFrameTicks, ticks, currentMax);
            if (observed == currentMax)
            {
                break;
            }
            currentMax = observed;
        }
    }

    public string FormatSummary(TimeSpan elapsed)
    {
        LatencySnapshot latency = m_moveResponseLatency.Snapshot();
        return string.Create(
            System.Globalization.CultureInfo.InvariantCulture,
            $"elapsed={elapsed:hh\\:mm\\:ss} connected={ConnectSucceeded} auth={AuthSucceeded}/{AuthFailed} " +
            $"registered={RegisteredUsers} entered={MapEnterSucceeded} " +
            $"failedUsers={FailedUsers} moveSent={MoveSent} moveRp={MoveResponses} " +
            $"rejected={Volatile.Read(ref m_moveRejected)} superseded={Volatile.Read(ref m_moveSuperseded)} " +
            $"corrected={Volatile.Read(ref m_moveCorrected)} " +
            $"moveNoti={Volatile.Read(ref m_moveNotifications)} player(+/-)=" +
            $"{Volatile.Read(ref m_playerSpawnNotifications)}/{Volatile.Read(ref m_playerDespawnNotifications)} " +
            $"monster(+/-)={Volatile.Read(ref m_monsterSpawnNotifications)}/" +
            $"{Volatile.Read(ref m_monsterDespawnNotifications)} " +
            $"moveRpLatency(avg/p50<=/p95<=/max)={latency.AverageMs:F2}/{FormatBound(latency.P50UpperBoundMs)}/" +
            $"{FormatBound(latency.P95UpperBoundMs)}/{latency.MaxMs:F2}ms " +
            $"frameOverrun={Volatile.Read(ref m_frameOverruns)}/{Volatile.Read(ref m_frameCount)} " +
            $"frameMax={TimeSpan.FromTicks(Volatile.Read(ref m_maxFrameTicks)).TotalMilliseconds:F2}ms");
    }

    private static string FormatBound(int value) => value == int.MaxValue ? ">1000" : value.ToString();
}

internal readonly record struct MoveResultSample(bool Succeeded, bool Corrected, bool Superseded, TimeSpan Latency);
