#pragma once

namespace Foundation::Diagnostics
{
	using FTimingMetricIndex = std::uint16_t;

	struct STimingMetricsConfig
	{
		int flushIntervalSeconds = 60;
		std::vector<std::string> metricNames;
	};

	struct STimingTopSample
	{
		std::uint64_t elapsedNanoseconds = 0;
		std::uint64_t contextId = 0;
		std::uint32_t threadId = 0;
		std::int64_t startedEpochMicroseconds = 0;
		std::int64_t finishedEpochMicroseconds = 0;
	};

	struct STimingMetricAggregate
	{
		std::uint64_t sampleCount = 0;
		std::uint64_t totalElapsedNanoseconds = 0;
		std::uint64_t minElapsedNanoseconds = std::numeric_limits<std::uint64_t>::max();
		std::uint64_t maxElapsedNanoseconds = 0;
		std::array<STimingTopSample, 3> topSamples{};
	};

	struct STimingSnapshot
	{
		std::int64_t bucketStartEpochSeconds = 0;
		std::vector<STimingMetricAggregate> metricAggregates;
	};

	struct STimingPendingSample
	{
		FTimingMetricIndex metricIndex = 0;
		std::uint64_t contextId = 0;
		std::chrono::steady_clock::time_point startedSteady{};
		std::chrono::system_clock::time_point startedSystem{};
	};
}
