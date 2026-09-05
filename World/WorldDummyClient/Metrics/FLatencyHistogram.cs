namespace WorldDummyClient.Metrics;

internal sealed class FLatencyHistogram
{
    private static readonly int[] s_upperBoundsMs = [1, 2, 5, 10, 20, 50, 75, 100, 150, 250, 500, 1000, int.MaxValue];
    private readonly long[] m_buckets = new long[s_upperBoundsMs.Length];
    private long m_count;
    private long m_totalTicks;
    private long m_maxTicks;

    public void Record(TimeSpan latency)
    {
        long ticks = Math.Max(0, latency.Ticks);
        int milliseconds = (int)Math.Min(int.MaxValue, Math.Ceiling(latency.TotalMilliseconds));
        int bucketIndex = Array.FindIndex(s_upperBoundsMs, upperBound => milliseconds <= upperBound);
        Interlocked.Increment(ref m_buckets[bucketIndex]);
        Interlocked.Increment(ref m_count);
        Interlocked.Add(ref m_totalTicks, ticks);

        long currentMax = Volatile.Read(ref m_maxTicks);
        while (ticks > currentMax)
        {
            long observed = Interlocked.CompareExchange(ref m_maxTicks, ticks, currentMax);
            if (observed == currentMax)
            {
                break;
            }
            currentMax = observed;
        }
    }

    public LatencySnapshot Snapshot()
    {
        long count = Volatile.Read(ref m_count);
        if (count == 0)
        {
            return new LatencySnapshot(0, 0.0, 0, 0, 0.0);
        }

        long[] buckets = new long[m_buckets.Length];
        for (int index = 0; index < buckets.Length; ++index)
        {
            buckets[index] = Volatile.Read(ref m_buckets[index]);
        }

        return new LatencySnapshot(
            count,
            TimeSpan.FromTicks(Volatile.Read(ref m_totalTicks)).TotalMilliseconds / count,
            FindPercentileUpperBound(buckets, count, 0.50),
            FindPercentileUpperBound(buckets, count, 0.95),
            TimeSpan.FromTicks(Volatile.Read(ref m_maxTicks)).TotalMilliseconds);
    }

    private static int FindPercentileUpperBound(long[] buckets, long count, double percentile)
    {
        long target = Math.Max(1, (long)Math.Ceiling(count * percentile));
        long cumulative = 0;
        for (int index = 0; index < buckets.Length; ++index)
        {
            cumulative += buckets[index];
            if (cumulative >= target)
            {
                return s_upperBoundsMs[index];
            }
        }
        return int.MaxValue;
    }
}

internal readonly record struct LatencySnapshot(
    long Count,
    double AverageMs,
    int P50UpperBoundMs,
    int P95UpperBoundMs,
    double MaxMs);
