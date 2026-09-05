using WorldClientWinForms.Configuration;
using WorldClientWinForms.SmokeTests;

namespace WorldClientWinForms;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            ClientSettings settings = ClientSettings.Load();
            uint mapDataId = ParseMapDataId(args) ?? settings.DefaultMapDataId;
            if (args.Any(static argument => string.Equals(argument, "--auth-api-self-test", StringComparison.OrdinalIgnoreCase)))
            {
                return WorldLoginApiClientSelfTest.RunAsync().GetAwaiter().GetResult();
            }

            if (args.Any(static argument => string.Equals(argument, "--simulation-self-test", StringComparison.OrdinalIgnoreCase)))
            {
                return WorldSimulationSmokeTest.Run();
            }

            if (args.Any(static argument => string.Equals(argument, "--auth-smoke", StringComparison.OrdinalIgnoreCase)))
            {
                return WorldAuthSmokeTest.RunAsync(settings, args, mapDataId).GetAwaiter().GetResult();
            }

            if (args.Any(static argument => string.Equals(argument, "--smoke-test", StringComparison.OrdinalIgnoreCase)))
            {
                return ClientSmokeTest.RunAsync(settings, mapDataId).GetAwaiter().GetResult();
            }

            if (args.Any(static argument => string.Equals(argument, "--monster-smoke-test", StringComparison.OrdinalIgnoreCase)))
            {
                return ClientSmokeTest.RunMonsterSpawnAsync(settings, mapDataId).GetAwaiter().GetResult();
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            if (args.Any(static argument => string.Equals(argument, "--ui-smoke-test", StringComparison.OrdinalIgnoreCase)))
            {
                using var form = new MainForm(settings, "UI Smoke");
                form.Show();
                form.Activate();
                Application.DoEvents();

                if (Form.ActiveForm != form)
                {
                    throw new InvalidOperationException("UI smoke test could not activate the World client form.");
                }

                Message loginInputMessage = Message.Create(
                    form.Handle,
                    0x0100,
                    new IntPtr((int)Keys.W),
                    IntPtr.Zero);
                if (form.PreFilterMessage(ref loginInputMessage))
                {
                    throw new InvalidOperationException("Login UI consumed W as a world movement key.");
                }

                form.ShowMainView();
                Application.DoEvents();
                if (!form.HasReadableStatusPanel)
                {
                    throw new InvalidOperationException("World status panel was laid out below its readable minimum width.");
                }

                form.Close();
                return 0;
            }

            int clientCount = ParseClientCount(args);
            Application.Run(new MultiClientApplicationContext(settings, clientCount));
            return 0;
        }
        catch (Exception exception)
        {
            if (args.Any(static argument =>
                    string.Equals(argument, "--auth-api-self-test", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(argument, "--simulation-self-test", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(argument, "--auth-smoke", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(argument, "--smoke-test", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(argument, "--monster-smoke-test", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(argument, "--ui-smoke-test", StringComparison.OrdinalIgnoreCase)))
            {
                Console.Error.WriteLine(exception);
                return 1;
            }

            MessageBox.Show(
                exception.ToString(),
                "World Client startup failed",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }

    private static int ParseClientCount(IReadOnlyList<string> args)
    {
        for (int index = 0; index < args.Count; ++index)
        {
            if (!string.Equals(args[index], "--clients", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (index + 1 >= args.Count || !int.TryParse(args[index + 1], out int count) || count is < 1 or > 8)
            {
                throw new ArgumentException("--clients must be followed by a value from 1 to 8.");
            }
            return count;
        }
        return 1;
    }

    private static uint? ParseMapDataId(IReadOnlyList<string> args)
    {
        for (int index = 0; index < args.Count; ++index)
        {
            if (!string.Equals(args[index], "--map-data-id", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            if (index + 1 >= args.Count || !uint.TryParse(args[index + 1], out uint mapDataId) || mapDataId == 0)
            {
                throw new ArgumentException("--map-data-id must be followed by a positive integer.");
            }
            return mapDataId;
        }
        return null;
    }
}
