using WorldDummyClient.Configuration;
using WorldDummyClient.Runtime;

namespace WorldDummyClient;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        try
        {
            CommandLineOptions options = CommandLineOptions.Parse(args);
            WorldDummySettings settings = WorldDummySettings.Load(options.ConfigPath).ApplyOverrides(options);
            if (options.ValidateConfig)
            {
                Console.WriteLine(
                    $"[WorldDummy] config valid. endpoint={settings.WorldServerHost}:{settings.WorldServerPort}, " +
                    $"map={settings.MapDataId}, users={settings.VirtualUserCount}, workers={settings.WorkerCount}, " +
                    $"auth={(settings.UseLoginServerAuthentication ? "LoginServer" : "Disabled")}");
                return 0;
            }

            using var cancellation = new CancellationTokenSource();
            Console.CancelKeyPress += (_, eventArgs) =>
            {
                eventArgs.Cancel = true;
                cancellation.Cancel();
            };
            return await new FWorldDummyRunner(settings).RunAsync(cancellation.Token).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[WorldDummy] startup failed: {exception}");
            return 1;
        }
    }
}
