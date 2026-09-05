#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Diagnostics/FAuctionTimingSetup.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"

namespace
{
	struct SPacketDescriptor
	{
		std::uint16_t opcode = 0;
		std::string_view name;
	};

	struct SContentStageDescriptor
	{
		ContentsRuntime::Core::FContentId contentId = ContentsRuntime::Core::kInvalidContentId;
		std::string_view name;
	};

	constexpr std::array<SPacketDescriptor, 17> kPacketDescriptors = {{
		{Generated::Auction::FAuctionAuthRq::kOpcode, "AuctionAuthRq"},
		{Generated::Auction::FPingRq::kOpcode, "PingRq"},
		{Generated::Auction::FMyBidListRq::kOpcode, "MyBidListRq"},
		{Generated::Auction::FInventoryListRq::kOpcode, "InventoryListRq"},
		{Generated::Auction::FListingRegisterRq::kOpcode, "ListingRegisterRq"},
		{Generated::Auction::FListingSearchRq::kOpcode, "ListingSearchRq"},
		{Generated::Auction::FListingDetailRq::kOpcode, "ListingDetailRq"},
		{Generated::Auction::FBidRq::kOpcode, "BidRq"},
		{Generated::Auction::FBuyoutRq::kOpcode, "BuyoutRq"},
		{Generated::Auction::FMailListRq::kOpcode, "MailListRq"},
		{Generated::Auction::FMailDetailRq::kOpcode, "MailDetailRq"},
		{Generated::Auction::FMailClaimRq::kOpcode, "MailClaimRq"},
		{Generated::Auction::FListingCancelRq::kOpcode, "ListingCancelRq"},
		{Generated::Auction::FDebugCheatRq::kOpcode, "DebugCheatRq"},
		{Generated::Auction::FSaleHistorySearchRq::kOpcode, "SaleHistorySearchRq"},
		{Generated::Auction::FSaleHistoryDetailRq::kOpcode, "SaleHistoryDetailRq"},
		{Generated::Auction::FBidRefundRq::kOpcode, "BidRefundRq"},
	}};

	constexpr std::array<SContentStageDescriptor, 3> kContentStageDescriptors = {{
		{AuctionHouseServer::Contents::kAuthContentId, "Auth"},
		{AuctionHouseServer::Contents::kRouterContentId, "Router"},
		{AuctionHouseServer::Contents::kCommandContentId, "Command"},
	}};

	constexpr std::array<std::string_view, 24> kAuctionProcedureNames = {{
		"sp_ad_c_listing_prepare",
		"sp_ad_cu_bid_prepare",
		"sp_ad_cu_buyout_prepare",
		"sp_ad_r_expired_listing_candidates",
		"sp_ad_r_listing_detail",
		"sp_ad_r_listings",
		"sp_ad_r_my_bids",
		"sp_ad_r_outbid_claimable",
		"sp_ad_r_sale_history",
		"sp_ad_r_sale_history_detail",
		"sp_ad_u_bid_complete",
		"sp_ad_u_bid_revert",
		"sp_ad_u_bid_refund_complete",
		"sp_ad_u_bid_refund_prepare",
		"sp_ad_u_bid_refund_revert",
		"sp_ad_u_buyout_complete",
		"sp_ad_u_buyout_revert",
		"sp_ad_u_cancel_complete",
		"sp_ad_u_cancel_prepare",
		"sp_ad_u_cancel_revert",
		"sp_ad_u_expire_complete",
		"sp_ad_u_expire_prepare",
		"sp_ad_u_expire_revert",
		"sp_ad_u_listing_activate",
	}};

	std::string BuildPacketMetricName(
		const std::string_view contentStage,
		const std::string_view packetName,
		const std::string_view measurement)
	{
		return std::format("Content.{}.{}.{}", contentStage, packetName, measurement);
	}

	std::string BuildProcedureMetricName(
		const std::string_view procedureName)
	{
		return std::format("MySql.{}", procedureName);
	}

	Foundation::Diagnostics::FTimingMetricIndex FindRequiredMetricIndex(
		const Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime,
		const std::string_view metricName)
	{
		const auto metricIndex = timingMetricsRuntime.FindMetricIndex(metricName);
		if (!metricIndex.has_value())
		{
			throw std::logic_error(std::format("timing metric is not registered: {}", metricName));
		}

		return *metricIndex;
	}

	template <std::size_t TProcedureCount>
	std::shared_ptr<const Connector::MySql::SMySqlTimingConfig> BuildMySqlTimingConfig(
		Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime,
		const std::string_view databaseRole,
		const std::array<std::string_view, TProcedureCount>& procedureNames)
	{
		auto timingConfig = std::make_shared<Connector::MySql::SMySqlTimingConfig>();
		timingConfig->timingMetricsRuntime = &timingMetricsRuntime;
		for (const std::string_view procedureName : procedureNames)
		{
			timingConfig->procedureMetricIndices.emplace(
				procedureName, FindRequiredMetricIndex(timingMetricsRuntime, BuildProcedureMetricName(procedureName)));
		}

		timingConfig->connectMetricIndex = FindRequiredMetricIndex(timingMetricsRuntime, std::format("MySql.{}.Connect", databaseRole));
		timingConfig->beginMetricIndex = FindRequiredMetricIndex(timingMetricsRuntime, std::format("MySql.{}.Begin", databaseRole));
		timingConfig->commitMetricIndex = FindRequiredMetricIndex(timingMetricsRuntime, std::format("MySql.{}.Commit", databaseRole));
		timingConfig->rollbackMetricIndex = FindRequiredMetricIndex(timingMetricsRuntime, std::format("MySql.{}.Rollback", databaseRole));
		return timingConfig;
	}

	void ApplyTimingConfig(
		Connector::MySql::SMySqlConnectionConfig& primary,
		std::vector<Connector::MySql::SMySqlConnectionConfig>& replicas,
		const std::shared_ptr<const Connector::MySql::SMySqlTimingConfig>& timingConfig)
	{
		primary.timingConfig = timingConfig;
		for (Connector::MySql::SMySqlConnectionConfig& replica : replicas)
		{
			replica.timingConfig = timingConfig;
		}
	}
}

namespace AuctionHouseServer::Diagnostics
{
	Foundation::Diagnostics::STimingMetricsConfig BuildAuctionTimingMetricsConfig(
		const int flushIntervalSeconds)
	{
		Foundation::Diagnostics::STimingMetricsConfig config{};
		config.flushIntervalSeconds = flushIntervalSeconds;
		config.metricNames.reserve(kContentStageDescriptors.size() * kPacketDescriptors.size() * 2 + kAuctionProcedureNames.size() + 4);

		for (const SContentStageDescriptor& contentStage : kContentStageDescriptors)
		{
			for (const SPacketDescriptor& packet : kPacketDescriptors)
			{
				config.metricNames.push_back(BuildPacketMetricName(contentStage.name, packet.name, "QueueWait"));
				config.metricNames.push_back(BuildPacketMetricName(contentStage.name, packet.name, "Handler"));
			}
		}

		for (const std::string_view procedureName : kAuctionProcedureNames)
		{
			config.metricNames.push_back(BuildProcedureMetricName(procedureName));
		}

		config.metricNames.push_back("MySql.Auction.Connect");
		config.metricNames.push_back("MySql.Auction.Begin");
		config.metricNames.push_back("MySql.Auction.Commit");
		config.metricNames.push_back("MySql.Auction.Rollback");

		return config;
	}

	std::vector<ContentsRuntime::Core::SPacketTimingMetricConfig> BuildAuctionPacketTimingMetrics(
		const Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime)
	{
		std::vector<ContentsRuntime::Core::SPacketTimingMetricConfig> timingMetrics;
		timingMetrics.reserve(kContentStageDescriptors.size() * kPacketDescriptors.size());
		for (const SContentStageDescriptor& contentStage : kContentStageDescriptors)
		{
			for (const SPacketDescriptor& packet : kPacketDescriptors)
			{
				ContentsRuntime::Core::SPacketTimingMetricConfig timingMetric{};
				timingMetric.contentId = contentStage.contentId;
				timingMetric.opcode = packet.opcode;
				timingMetric.queueWaitMetricIndex = static_cast<std::uint16_t>(
					FindRequiredMetricIndex(timingMetricsRuntime, BuildPacketMetricName(contentStage.name, packet.name, "QueueWait")));
				timingMetric.handlerMetricIndex = static_cast<std::uint16_t>(
					FindRequiredMetricIndex(timingMetricsRuntime, BuildPacketMetricName(contentStage.name, packet.name, "Handler")));
				timingMetrics.push_back(timingMetric);
			}
		}

		return timingMetrics;
	}

	void ConfigureAuctionDatabaseTiming(
		Database::SAuctionDatabaseConfig& databaseConfig,
		Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime)
	{
		const auto auctionTimingConfig = BuildMySqlTimingConfig(timingMetricsRuntime, "Auction", kAuctionProcedureNames);
		ApplyTimingConfig(databaseConfig.auctionPrimary, databaseConfig.auctionReplicas, auctionTimingConfig);
	}
}
