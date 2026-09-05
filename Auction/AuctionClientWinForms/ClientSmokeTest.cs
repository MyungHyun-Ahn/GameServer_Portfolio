using AuctionClientWinForms.Configuration;
using AuctionClientWinForms.Models;
using AuctionClientWinForms.Networking;

namespace AuctionClientWinForms;

internal static class ClientSmokeTest
{
	private const int PaginationPageSize = 20;
	private const uint PaginationFetchSize = PaginationPageSize + 1;
	private const uint PaginationItemDataId = 999001;
	private const ushort ListingLimitExceeded = 30;

	public static async Task<int> RunListingLimitAsync(ClientSettings settings)
	{
		string suffix = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString();
		string loginId = $"auction_limit_{suffix}";
		const string password = "Portfolio!12345";

		try
		{
			AuthApiClient authApi = new();
			await authApi.RegisterAsync(
				new AuthServerSettings(settings.LoginServerBaseUrl),
				new RegisterAccountRequest(loginId, password, $"Limit-{suffix[^6..]}"));
			LoginAccountResponse login = await authApi.LoginAsync(
				new AuthServerSettings(settings.LoginServerBaseUrl),
				new LoginAccountRequest(loginId, password));

			await using AuctionTcpClient client = new();
			await client.ConnectAsync(new AuctionConnectionSettings(
				login.AuctionServer.Ip,
				login.AuctionServer.Port,
				settings.AuctionPacketKey));
			AuctionAuthResult auth = await client.AuthenticateAsync(login.AuctionTicket);
			if (auth.ResultCode != 0 || auth.MaxActiveListings == 0 || auth.DefaultCurrencyId == 0)
				return 1;
			ulong startPrice = GetListingPrice(auth, 1_000);
			ulong buyoutPrice = GetBuyoutPrice(auth, startPrice, 2_000);

			List<ulong> createdItemIds = [];
			for (uint index = 0; index <= auth.MaxActiveListings; ++index)
			{
				DebugCheatResult cheat = await client.ExecuteDebugCheatAsync(2, 0, 1001, 10 + index, 0, 0, 0);
				if (cheat.ResultCode != 0 || cheat.ItemInstanceId == 0)
					return 1;
				createdItemIds.Add(cheat.ItemInstanceId);
			}

			var inventory = await client.GetInventoryAsync();
			if (inventory.ResultCode != 0)
				return 1;
			Dictionary<ulong, InventoryItem> items = inventory.Items
				.Where(item => createdItemIds.Contains(item.ItemInstanceId))
				.ToDictionary(item => item.ItemInstanceId);
			if (items.Count != auth.MaxActiveListings + 1)
				return 1;

			for (int index = 0; index < auth.MaxActiveListings; ++index)
			{
				ListingRegisterResult result = await client.RegisterListingAsync(
					items[createdItemIds[index]], auth.DefaultCurrencyId, startPrice, buyoutPrice,
					auth.MinimumListingDurationSeconds);
				if (result.ResultCode != 0)
					return 1;
			}

			ListingRegisterResult rejected = await client.RegisterListingAsync(
				items[createdItemIds[checked((int)auth.MaxActiveListings)]],
				auth.DefaultCurrencyId,
				startPrice,
				buyoutPrice,
				auth.MinimumListingDurationSeconds);
			if (rejected.ResultCode != ListingLimitExceeded)
				return 1;

			var inventoryAfterRejection = await client.GetInventoryAsync();
			if (inventoryAfterRejection.ResultCode != 0 ||
				inventoryAfterRejection.Items.All(
					item => item.ItemInstanceId != createdItemIds[checked((int)auth.MaxActiveListings)]))
			{
				return 1;
			}

			Console.WriteLine($"LISTING_LIMIT_TEST_SUCCESS userId={login.UserId} active={auth.MaxActiveListings} rejectedCode={rejected.ResultCode} inventoryRollback=1 loginId={loginId}");
			return 0;
		}
		catch (Exception exception)
		{
			Console.Error.WriteLine(exception);
			return 1;
		}
	}

	public static async Task<int> RunPaginationAsync(ClientSettings settings)
	{
		string suffix = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString();
		string loginId = $"auction_pagination_{suffix}";
		const string password = "Portfolio!12345";

		try
		{
			AuthApiClient authApi = new();
			await authApi.RegisterAsync(
				new AuthServerSettings(settings.LoginServerBaseUrl),
				new RegisterAccountRequest(loginId, password, $"Page-{suffix[^6..]}"));
			LoginAccountResponse login = await authApi.LoginAsync(
				new AuthServerSettings(settings.LoginServerBaseUrl),
				new LoginAccountRequest(loginId, password));

			await using AuctionTcpClient client = new();
			await client.ConnectAsync(new AuctionConnectionSettings(
				login.AuctionServer.Ip,
				login.AuctionServer.Port,
				settings.AuctionPacketKey));
			AuctionAuthResult auth = await client.AuthenticateAsync(login.AuctionTicket);
			if (auth.ResultCode != 0)
				return 1;

			for (byte sortType = 1; sortType <= 4; ++sortType)
			{
				List<int> pageCounts = [];
				List<ListingSummary> allListings = [];
				(ulong SortValue, ulong ListingId) cursor = (0, 0);
				while (true)
				{
					var result = await client.SearchListingsAsync(
						0, [PaginationItemDataId], 0, 0, 0, 0,
						sellerOnly: false, sortType: sortType, cursorSortValue: cursor.SortValue,
						cursorListingId: cursor.ListingId, limit: PaginationFetchSize);
					if (result.ResultCode != 0)
						return 1;
					ListingSummary[] page = result.Listings.Take(PaginationPageSize).ToArray();
					pageCounts.Add(page.Length);
					allListings.AddRange(page);
					if (result.Listings.Count <= PaginationPageSize)
						break;
					ListingSummary last = page[^1];
					cursor = (GetListingSortValue(last, sortType), last.ListingId);
				}
				if (!pageCounts.SequenceEqual([20, 20, 5]) || allListings.Select(item => item.ListingId).Distinct().Count() != 45 ||
					!IsListingOrderValid(allListings, sortType))
				{
					return 1;
				}
			}

			for (byte sortType = 1; sortType <= 3; ++sortType)
			{
				List<int> pageCounts = [];
				List<SaleHistorySummary> allHistory = [];
				(ulong SortValue, ulong ListingId) cursor = (0, 0);
				while (true)
				{
					var result = await client.SearchSaleHistoryAsync(
						0, [PaginationItemDataId], 0, 0, 0, 0,
						sortType: sortType, cursorSortValue: cursor.SortValue,
						cursorListingId: cursor.ListingId, limit: PaginationFetchSize);
					if (result.ResultCode != 0)
						return 1;
					SaleHistorySummary[] page = result.History.Take(PaginationPageSize).ToArray();
					pageCounts.Add(page.Length);
					allHistory.AddRange(page);
					if (result.History.Count <= PaginationPageSize)
						break;
					SaleHistorySummary last = page[^1];
					cursor = (sortType == 1 ? last.SoldAtUnixMs : last.FinalPrice, last.ListingId);
				}
				if (!pageCounts.SequenceEqual([20, 20, 5]) || allHistory.Select(item => item.ListingId).Distinct().Count() != 45 ||
					!IsHistoryOrderValid(allHistory, sortType))
				{
					return 1;
				}
			}

			return 0;
		}
		catch (Exception exception)
		{
			Console.Error.WriteLine(exception);
			return 1;
		}
	}

	private static ulong GetListingSortValue(ListingSummary listing, byte sortType) => sortType switch
	{
		2 or 3 => listing.CurrentBidPrice == 0 ? listing.StartPrice : listing.CurrentBidPrice,
		4 => listing.ExpiresAtUnixMs,
		_ => listing.ListingId
	};

	private static bool IsListingOrderValid(IReadOnlyList<ListingSummary> listings, byte sortType)
	{
		IEnumerable<ulong> actual = listings.Select(item => item.ListingId);
		IEnumerable<ulong> expected = sortType switch
		{
			2 => listings.OrderBy(item => GetListingSortValue(item, sortType)).ThenByDescending(item => item.ListingId).Select(item => item.ListingId),
			3 => listings.OrderByDescending(item => GetListingSortValue(item, sortType)).ThenBy(item => item.ListingId).Select(item => item.ListingId),
			4 => listings.OrderBy(item => item.ExpiresAtUnixMs).ThenBy(item => item.ListingId).Select(item => item.ListingId),
			_ => listings.OrderByDescending(item => item.ListingId).Select(item => item.ListingId)
		};
		return actual.SequenceEqual(expected);
	}

	private static bool IsHistoryOrderValid(IReadOnlyList<SaleHistorySummary> history, byte sortType)
	{
		IEnumerable<ulong> actual = history.Select(item => item.ListingId);
		IEnumerable<ulong> expected = sortType switch
		{
			2 => history.OrderBy(item => item.FinalPrice).ThenByDescending(item => item.ListingId).Select(item => item.ListingId),
			3 => history.OrderByDescending(item => item.FinalPrice).ThenBy(item => item.ListingId).Select(item => item.ListingId),
			_ => history.OrderByDescending(item => item.SoldAtUnixMs).ThenByDescending(item => item.ListingId).Select(item => item.ListingId)
		};
		return actual.SequenceEqual(expected);
	}

    public static async Task<int> RunReplicaRoutingAsync(ClientSettings settings)
    {
        string suffix = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString();
        string loginId = $"auction_replica_{suffix}";
        const string password = "Portfolio!12345";

        try
        {
            AuthApiClient authApi = new();
            await authApi.RegisterAsync(
                new AuthServerSettings(settings.LoginServerBaseUrl),
                new RegisterAccountRequest(loginId, password, $"Replica-{suffix[^6..]}"));
            LoginAccountResponse login = await authApi.LoginAsync(
                new AuthServerSettings(settings.LoginServerBaseUrl),
                new LoginAccountRequest(loginId, password));

            await using AuctionTcpClient client = new();
            await client.ConnectAsync(new AuctionConnectionSettings(
                login.AuctionServer.Ip,
                login.AuctionServer.Port,
                settings.AuctionPacketKey));
            AuctionAuthResult auth = await client.AuthenticateAsync(login.AuctionTicket);
            if (auth.ResultCode != 0 || auth.UserId != login.UserId)
                return 1;

            var inventory = await client.GetInventoryAsync();
            var listings = await client.SearchListingsAsync(0, Array.Empty<uint>(), 0, 0, 0, 0);
            return inventory.ResultCode == 0 && listings.ResultCode == 0 ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    public static async Task<int> RunAsync(ClientSettings settings)
    {
        string suffix = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds().ToString();
        string loginId = $"auction_ui_{suffix}";
        const string password = "Portfolio!12345";

        try
        {
            AuthApiClient authApi = new();
            await authApi.RegisterAsync(
                new AuthServerSettings(settings.LoginServerBaseUrl),
                new RegisterAccountRequest(loginId, password, $"UI-{suffix[^6..]}"));
            LoginAccountResponse login = await authApi.LoginAsync(
                new AuthServerSettings(settings.LoginServerBaseUrl),
                new LoginAccountRequest(loginId, password));

            await using AuctionTcpClient client = new();
            await client.ConnectAsync(new AuctionConnectionSettings(
                login.AuctionServer.Ip,
                login.AuctionServer.Port,
                settings.AuctionPacketKey));
            AuctionAuthResult auth = await client.AuthenticateAsync(login.AuctionTicket);
            if (auth.ResultCode != 0 || auth.UserId != login.UserId)
            {
                Console.Error.WriteLine($"Auction auth failed. result={auth.ResultCode} expected={login.UserId} actual={auth.UserId}");
                return 1;
            }

            DebugCheatResult goldCheat = await client.ExecuteDebugCheatAsync(1, 10_000, 0, 0, 0, 0, 0);
            DebugCheatResult materialCheat = await client.ExecuteDebugCheatAsync(2, 0, 3002, 0, 0, 0, 0);
            DebugCheatResult equipmentCheat = await client.ExecuteDebugCheatAsync(2, 0, 1001, 20, 3, 2, 1);
            if (goldCheat.ResultCode != 0 || goldCheat.CurrencyBalance != 10_000 ||
                materialCheat.ResultCode != 0 || materialCheat.ItemInstanceId == 0 ||
                equipmentCheat.ResultCode != 0 || equipmentCheat.ItemInstanceId == 0)
            {
                Console.Error.WriteLine("Debug cheat validation failed.");
                return 1;
            }

            var inventory = await client.GetInventoryAsync();
            InventoryItem? material = inventory.Items.FirstOrDefault(item => item.ItemInstanceId == materialCheat.ItemInstanceId);
            InventoryItem? equipment = inventory.Items.FirstOrDefault(item => item.ItemInstanceId == equipmentCheat.ItemInstanceId);
            if (inventory.ResultCode != 0 || material is null || equipment is null)
            {
                Console.Error.WriteLine("Cheat inventory validation failed.");
                return 1;
            }

            ulong materialStartPrice = GetListingPrice(auth, 100);
            ulong equipmentStartPrice = GetListingPrice(auth, 1_000);
            ListingRegisterResult materialListing = await client.RegisterListingAsync(
                material,
                auth.DefaultCurrencyId,
                materialStartPrice,
                GetBuyoutPrice(auth, materialStartPrice, 1_000),
                auth.DefaultListingDurationSeconds);
            ListingRegisterResult equipmentListing = await client.RegisterListingAsync(
                equipment,
                auth.DefaultCurrencyId,
                equipmentStartPrice,
                GetBuyoutPrice(auth, equipmentStartPrice, 5_000),
                auth.DefaultListingDurationSeconds);
            if (materialListing.ResultCode != 0 || equipmentListing.ResultCode != 0)
            {
                Console.Error.WriteLine("Cheat item listing validation failed.");
                return 1;
            }

            var search = await client.SearchListingsAsync(0, Array.Empty<uint>(), 0, 0, 0, 0);
            if (search.ResultCode != 0)
            {
                Console.Error.WriteLine($"Listing search failed. result={search.ResultCode}");
                return 1;
            }

            var magicSearch = await client.SearchListingsAsync(0, new uint[] { 3002 }, 0, 0, 0, 0);
            if (magicSearch.ResultCode != 0 || magicSearch.Listings.Count == 0 ||
                magicSearch.Listings.Any(listing => !listing.Name.StartsWith("Magic", StringComparison.OrdinalIgnoreCase)) ||
                magicSearch.Listings.Any(listing => string.IsNullOrWhiteSpace(listing.SellerLoginId)))
            {
                Console.Error.WriteLine("Listing name/seller search validation failed.");
                return 1;
            }

            foreach (string fragment in new[] { "Cl", "Ma", "gi" })
            {
                var fragmentSearch = await client.SearchListingsAsync(0, new uint[] { 3002 }, 0, 0, 0, 0);
                if (fragmentSearch.ResultCode != 0 ||
                    fragmentSearch.Listings.All(listing => !listing.Name.Contains(fragment, StringComparison.OrdinalIgnoreCase)))
                {
                    Console.Error.WriteLine($"Listing substring search validation failed. fragment={fragment}");
                    return 1;
                }
            }

            var detail = await client.GetListingDetailAsync(magicSearch.Listings[0].ListingId);
            if (detail.ResultCode != 0 || detail.Detail is null ||
                string.IsNullOrWhiteSpace(detail.Detail.SellerLoginId))
            {
                Console.Error.WriteLine("Listing detail seller validation failed.");
                return 1;
            }

            var statSearch = await client.SearchListingsAsync(1, Array.Empty<uint>(), 20, 3, 2, 1);
            if (statSearch.ResultCode != 0 || statSearch.Listings.Count == 0 ||
                statSearch.Listings.Any(listing => listing.Strength < 20 || listing.Dexterity < 3 ||
                                                   listing.Intelligence < 2 || listing.Luck < 1))
            {
                Console.Error.WriteLine("Listing minimum stat validation failed.");
                return 1;
            }

            Console.WriteLine(
                $"AUCTION_WINFORMS_SMOKE_SUCCESS userId={auth.UserId} listings={search.Listings.Count} " +
                $"magic={magicSearch.Listings.Count} customStats={statSearch.Listings.Count} gold={goldCheat.CurrencyBalance}");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

	private static ulong GetListingPrice(AuctionAuthResult auth, ulong preferredPrice) =>
		Math.Clamp(preferredPrice, auth.MinimumListingPrice, auth.MaximumListingPrice);

	private static ulong GetBuyoutPrice(AuctionAuthResult auth, ulong startPrice, ulong preferredPrice) =>
		Math.Clamp(preferredPrice, startPrice, auth.MaximumListingPrice);
}
