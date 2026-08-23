namespace AuctionClientWinForms;

using AuctionClientWinForms.Configuration;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        ClientSettings settings;
        try
        {
            settings = ClientSettings.Load();
        }
        catch (Exception exception)
        {
            ApplicationConfiguration.Initialize();
            MessageBox.Show(exception.Message, "Auction Client 설정 오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        if (args.Contains("--smoke-test", StringComparer.OrdinalIgnoreCase))
        {
            return ClientSmokeTest.RunAsync(settings).GetAwaiter().GetResult();
        }

        if (args.Contains("--replica-routing-smoke", StringComparer.OrdinalIgnoreCase))
        {
            return ClientSmokeTest.RunReplicaRoutingAsync(settings).GetAwaiter().GetResult();
        }

		if (args.Contains("--pagination-smoke", StringComparer.OrdinalIgnoreCase))
		{
			return ClientSmokeTest.RunPaginationAsync(settings).GetAwaiter().GetResult();
		}
		if (args.Contains("--listing-limit-smoke", StringComparer.OrdinalIgnoreCase))
		{
			return ClientSmokeTest.RunListingLimitAsync(settings).GetAwaiter().GetResult();
		}

        ApplicationConfiguration.Initialize();
        try
        {
            Application.Run(new MainForm(settings));
            return 0;
        }
        catch (Exception exception)
        {
            MessageBox.Show(
                exception.ToString(),
                "Auction Client 시작 오류",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }
}
