#pragma once

namespace Generated::Config::AuctionDummyClient
{
	struct SAuctionDummyClientAuctionDummyConfig
	{
		std::string ServerIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19100);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(4);
		std::int32_t VirtualUserCount = static_cast<std::int32_t>(100);
		std::int32_t ConnectsPerSecond = static_cast<std::int32_t>(20);
		std::int32_t RunSeconds = static_cast<std::int32_t>(60);
		std::int32_t SearchIntervalMinMs = static_cast<std::int32_t>(500);
		std::int32_t SearchIntervalMaxMs = static_cast<std::int32_t>(1500);
		std::int32_t ResponseTimeoutMs = static_cast<std::int32_t>(5000);
		std::int32_t ConsoleSummaryIntervalSeconds = static_cast<std::int32_t>(5);
		std::int32_t EventPollMaxCount = static_cast<std::int32_t>(512);
		std::int32_t RandomStatMaximum = static_cast<std::int32_t>(20);
		std::int32_t SearchWeight = static_cast<std::int32_t>(50);
		std::int32_t RegisterWeight = static_cast<std::int32_t>(20);
		std::int32_t BidWeight = static_cast<std::int32_t>(30);
		std::uint64_t InitialGoldAmount = static_cast<std::uint64_t>(1000000);
		std::uint32_t BidIncrementMinimum = static_cast<std::uint32_t>(10);
		std::uint32_t BidIncrementMaximum = static_cast<std::uint32_t>(100);
		std::int32_t BidHotspotPercent = static_cast<std::int32_t>(80);
		std::int32_t OutbidRefundPercent = static_cast<std::int32_t>(50);
		std::int32_t InventoryListLimit = static_cast<std::int32_t>(100);
		std::string CheatItemDataIds = "1001,1002,1003,1004,2001,2002,3001,3002";
		std::uint32_t ListingStartPriceMinimum = static_cast<std::uint32_t>(100);
		std::uint32_t ListingStartPriceMaximum = static_cast<std::uint32_t>(1000);
		std::uint32_t ListingBuyoutMarkupMinimum = static_cast<std::uint32_t>(100);
		std::uint32_t ListingBuyoutMarkupMaximum = static_cast<std::uint32_t>(1000);
		std::uint32_t RandomSeed = static_cast<std::uint32_t>(20260817);
		std::string TicketFilePath = "Tickets.txt";
	};

	struct FAuctionDummyClientConfigDocument
	{
		SAuctionDummyClientAuctionDummyConfig AuctionDummy;
	};

	class FAuctionDummyClientConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath,
			FAuctionDummyClientConfigDocument& outConfig,
			std::string& outError);
	};
}
