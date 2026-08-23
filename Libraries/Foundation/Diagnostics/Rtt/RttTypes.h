#pragma once

namespace Foundation::Diagnostics
{
	using FRttStageIndex = std::uint16_t;

	struct SRttMetricsConfig
	{
		int flushIntervalSeconds = 60;
		std::vector<std::string> stageNames;
	};

	struct SRttTopSample
	{
		double rttMs = 0.0;
		int sessionIndex = -1;
		std::int64_t sentEpochMs = 0;
		std::int64_t recvEpochMs = 0;
	};

	struct SRttStageAggregate
	{
		std::uint64_t sampleCount = 0;
		std::uint64_t timeoutCount = 0;
		double totalRttMs = 0.0;
		std::array<SRttTopSample, 3> topSamples{};
	};

	struct SRttSnapshot
	{
		std::int64_t bucketStartEpochSeconds = 0;
		std::vector<SRttStageAggregate> stageAggregates;
	};

	struct SRttPendingRequest
	{
		FRttStageIndex stageIndex = 0;
		int sessionIndex = -1;
		std::chrono::steady_clock::time_point sentSteady{};
		std::chrono::system_clock::time_point sentSystem{};
	};
}
