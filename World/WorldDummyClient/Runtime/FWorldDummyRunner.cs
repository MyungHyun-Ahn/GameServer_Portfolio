using System.Diagnostics;
using WorldClientCore.Authentication;
using WorldDummyClient.Configuration;
using WorldDummyClient.Metrics;

namespace WorldDummyClient.Runtime;

internal sealed class FWorldDummyRunner(WorldDummySettings settings)
{
    private readonly WorldDummySettings m_settings = settings;
    private readonly FWorldDummyMetrics m_metrics = new();
    private readonly WorldLoginApiClient m_loginApiClient = new();
    private readonly List<FWorldDummyWorker> m_workers = [];
    private readonly List<FVirtualWorldUser> m_users = [];

    public async Task<int> RunAsync(CancellationToken cancellationToken)
    {
        using var workerStopSource = new CancellationTokenSource();
        BuildWorkersAndUsers();
        Task[] workerTasks = m_workers
            .Select(worker => worker.RunAsync(workerStopSource.Token))
            .ToArray();

        var totalClock = Stopwatch.StartNew();
        try
        {
            Console.WriteLine(
                $"[WorldDummy] start map={m_settings.MapDataId}, users={m_settings.VirtualUserCount}, " +
                $"workers={m_workers.Count}, frame={m_settings.FrameRate}Hz, seed={m_settings.RandomSeed}, " +
                $"auth={(m_settings.UseLoginServerAuthentication ? "LoginServer" : "Disabled")}");

            await RampConnectionsAsync(cancellationToken).ConfigureAwait(false);
            await WaitForBootstrapAsync(cancellationToken).ConfigureAwait(false);

            Console.WriteLine(
                $"[WorldDummy] bootstrap connected={m_metrics.ConnectSucceeded}, " +
                $"auth={m_metrics.AuthSucceeded}/{m_metrics.AuthFailed}, registered={m_metrics.RegisteredUsers}, " +
                $"entered={m_metrics.MapEnterSucceeded}, failed={m_metrics.FailedUsers}");

            if (m_metrics.MapEnterSucceeded == m_settings.VirtualUserCount && m_metrics.FailedUsers == 0)
            {
                await RunScenarioAsync(cancellationToken).ConfigureAwait(false);
            }
            else
            {
                Console.Error.WriteLine("[WorldDummy] scenario skipped because bootstrap did not complete for every user.");
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            Console.WriteLine("[WorldDummy] cancellation requested.");
        }
        finally
        {
            foreach (FWorldDummyWorker worker in m_workers)
            {
                worker.RequestStopUsers();
            }
            await Task.Delay(500).ConfigureAwait(false);
            await DisposeUsersAsync().ConfigureAwait(false);
            await Task.Delay(TimeSpan.FromTicks(m_settings.FrameInterval.Ticks * 2)).ConfigureAwait(false);
            workerStopSource.Cancel();
            await Task.WhenAll(workerTasks).ConfigureAwait(false);
        }

        Console.WriteLine($"[WorldDummy] final {m_metrics.FormatSummary(totalClock.Elapsed)}");
        int scenarioSucceededUsers = m_users.Count(static user => user.ScenarioSucceeded);
        bool succeeded = !cancellationToken.IsCancellationRequested &&
            m_metrics.MapEnterSucceeded == m_settings.VirtualUserCount &&
            (!m_settings.UseLoginServerAuthentication ||
             (m_metrics.AuthSucceeded == m_settings.VirtualUserCount && m_metrics.AuthFailed == 0)) &&
            m_metrics.FailedUsers == 0 &&
            m_metrics.SendFailures == 0 &&
            m_metrics.MoveRejected == 0 &&
            m_metrics.MoveSent == m_metrics.MoveResponses &&
            scenarioSucceededUsers == m_settings.VirtualUserCount &&
            (m_settings.VirtualUserCount == 1 ||
             (m_metrics.PlayerSpawnNotifications > 0 && m_metrics.MoveNotifications > 0));
        Console.WriteLine(
            $"[WorldDummy] scenarioUsers={scenarioSucceededUsers}/{m_settings.VirtualUserCount}, " +
            $"pendingMoveResponses={m_metrics.MoveSent - m_metrics.MoveResponses}");
        if (!succeeded)
        {
            foreach (FVirtualWorldUser user in m_users.Where(static user => !user.ScenarioSucceeded).Take(10))
            {
                Console.Error.WriteLine($"[user {user.Index}] {user.ScenarioSummary}");
            }
        }
        Console.WriteLine(succeeded ? "[WorldDummy] PASS" : "[WorldDummy] FAIL");
        return succeeded ? 0 : 1;
    }

    private void BuildWorkersAndUsers()
    {
        int workerCount = Math.Min(m_settings.WorkerCount, m_settings.VirtualUserCount);
        for (int index = 0; index < workerCount; ++index)
        {
            m_workers.Add(new FWorldDummyWorker(index, m_settings, m_metrics));
        }

        for (int index = 0; index < m_settings.VirtualUserCount; ++index)
        {
            FWorldDummyWorker owner = m_workers[index % m_workers.Count];
            var user = new FVirtualWorldUser(index, m_settings, m_metrics, m_loginApiClient, owner.Enqueue);
            owner.AddUser(user);
            m_users.Add(user);
        }
    }

    private async Task RampConnectionsAsync(CancellationToken cancellationToken)
    {
        TimeSpan interval = TimeSpan.FromSeconds(1.0 / m_settings.ConnectsPerSecond);
        var connectTasks = new List<Task>(m_users.Count);
        foreach (FVirtualWorldUser user in m_users)
        {
            connectTasks.Add(user.StartConnectAsync(cancellationToken));
            if (interval > TimeSpan.Zero)
            {
                await Task.Delay(interval, cancellationToken).ConfigureAwait(false);
            }
        }
        await Task.WhenAll(connectTasks).ConfigureAwait(false);
    }

    private async Task WaitForBootstrapAsync(CancellationToken cancellationToken)
    {
        var timeoutClock = Stopwatch.StartNew();
        TimeSpan timeout = TimeSpan.FromMilliseconds(m_settings.ResponseTimeoutMs);
        while (m_metrics.MapEnterSucceeded + m_metrics.FailedUsers < m_settings.VirtualUserCount &&
               timeoutClock.Elapsed < timeout)
        {
            await Task.Delay(25, cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task RunScenarioAsync(CancellationToken cancellationToken)
    {
        var scenarioClock = Stopwatch.StartNew();
        TimeSpan duration = TimeSpan.FromSeconds(m_settings.RunSeconds);
        TimeSpan summaryInterval = TimeSpan.FromSeconds(m_settings.ConsoleSummaryIntervalSeconds);
        while (scenarioClock.Elapsed < duration)
        {
            TimeSpan remaining = duration - scenarioClock.Elapsed;
            await Task.Delay(remaining < summaryInterval ? remaining : summaryInterval, cancellationToken)
                .ConfigureAwait(false);
            Console.WriteLine($"[WorldDummy] {m_metrics.FormatSummary(scenarioClock.Elapsed)}");
        }
    }

    private async Task DisposeUsersAsync()
    {
        await Parallel.ForEachAsync(
            m_users,
            new ParallelOptions { MaxDegreeOfParallelism = Math.Max(1, m_workers.Count * 4) },
            async (user, _) => await user.DisposeAsync().ConfigureAwait(false)).ConfigureAwait(false);
    }
}
