#include "AuctionDummyClientPch.h"

#include "Generated/Config/AuctionDummyClient/AuctionDummyClientConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::AuctionDummyClient
{
	bool FAuctionDummyClientConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FAuctionDummyClientConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 1> kKnownSections = {"AuctionDummy"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 29> kAuctionDummyKnownKeys = {"ServerIp",
			"Port",
			"PacketKey",
			"WorkerThreadCount",
			"VirtualUserCount",
			"ConnectsPerSecond",
			"RunSeconds",
			"SearchIntervalMinMs",
			"SearchIntervalMaxMs",
			"ResponseTimeoutMs",
			"ConsoleSummaryIntervalSeconds",
			"EventPollMaxCount",
			"RandomStatMaximum",
			"SearchWeight",
			"RegisterWeight",
			"BidWeight",
			"InitialGoldAmount",
			"BidIncrementMinimum",
			"BidIncrementMaximum",
			"BidHotspotPercent",
			"OutbidRefundPercent",
			"InventoryListLimit",
			"CheatItemDataIds",
			"ListingStartPriceMinimum",
			"ListingStartPriceMaximum",
			"ListingBuyoutMarkupMinimum",
			"ListingBuyoutMarkupMaximum",
			"RandomSeed",
			"TicketFilePath"};

		if (!reader.ValidateKnownKeys("AuctionDummy", kAuctionDummyKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDummy", "ServerIp", outConfig.AuctionDummy.ServerIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("AuctionDummy", "Port", outConfig.AuctionDummy.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionDummy", "PacketKey", outConfig.AuctionDummy.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "WorkerThreadCount", outConfig.AuctionDummy.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "VirtualUserCount", outConfig.AuctionDummy.VirtualUserCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "ConnectsPerSecond", outConfig.AuctionDummy.ConnectsPerSecond, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "RunSeconds", outConfig.AuctionDummy.RunSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "SearchIntervalMinMs", outConfig.AuctionDummy.SearchIntervalMinMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "SearchIntervalMaxMs", outConfig.AuctionDummy.SearchIntervalMaxMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "ResponseTimeoutMs", outConfig.AuctionDummy.ResponseTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"AuctionDummy", "ConsoleSummaryIntervalSeconds", outConfig.AuctionDummy.ConsoleSummaryIntervalSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "EventPollMaxCount", outConfig.AuctionDummy.EventPollMaxCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "RandomStatMaximum", outConfig.AuctionDummy.RandomStatMaximum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "SearchWeight", outConfig.AuctionDummy.SearchWeight, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "RegisterWeight", outConfig.AuctionDummy.RegisterWeight, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "BidWeight", outConfig.AuctionDummy.BidWeight, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt64("AuctionDummy", "InitialGoldAmount", outConfig.AuctionDummy.InitialGoldAmount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionDummy", "BidIncrementMinimum", outConfig.AuctionDummy.BidIncrementMinimum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionDummy", "BidIncrementMaximum", outConfig.AuctionDummy.BidIncrementMaximum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "BidHotspotPercent", outConfig.AuctionDummy.BidHotspotPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "OutbidRefundPercent", outConfig.AuctionDummy.OutbidRefundPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionDummy", "InventoryListLimit", outConfig.AuctionDummy.InventoryListLimit, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("AuctionDummy", "CheatItemDataIds", outConfig.AuctionDummy.CheatItemDataIds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDummy", "ListingStartPriceMinimum", outConfig.AuctionDummy.ListingStartPriceMinimum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDummy", "ListingStartPriceMaximum", outConfig.AuctionDummy.ListingStartPriceMaximum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDummy", "ListingBuyoutMarkupMinimum", outConfig.AuctionDummy.ListingBuyoutMarkupMinimum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDummy", "ListingBuyoutMarkupMaximum", outConfig.AuctionDummy.ListingBuyoutMarkupMaximum, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionDummy", "RandomSeed", outConfig.AuctionDummy.RandomSeed, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDummy", "TicketFilePath", outConfig.AuctionDummy.TicketFilePath, outError))
		{
			return false;
		}

		return true;
	}
}
