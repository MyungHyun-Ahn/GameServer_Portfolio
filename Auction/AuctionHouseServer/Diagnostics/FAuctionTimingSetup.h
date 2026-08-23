#pragma once

namespace AuctionHouseServer::Diagnostics
{
	Foundation::Diagnostics::STimingMetricsConfig BuildAuctionTimingMetricsConfig(int flushIntervalSeconds);
	std::vector<ContentsRuntime::Core::SPacketTimingMetricConfig> BuildAuctionPacketTimingMetrics(
		const Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime);
	void ConfigureAuctionDatabaseTiming(Database::SAuctionDatabaseConfig& databaseConfig,
		Foundation::Diagnostics::FTimingMetricsRuntime& timingMetricsRuntime);
}
