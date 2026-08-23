#pragma once

namespace Foundation::Diagnostics
{
	class FTimingMetricsRuntime;

	class FTimingCsvLogger
	{
	public:
		FTimingCsvLogger(FTimingMetricsRuntime& timingMetricsRuntime, std::string csvPath);
		~FTimingCsvLogger();

		FTimingCsvLogger(const FTimingCsvLogger&) = delete;
		FTimingCsvLogger& operator=(const FTimingCsvLogger&) = delete;

		void Start();
		void Stop();

	private:
		void Run();

		FTimingMetricsRuntime& m_timingMetricsRuntime;
		std::string m_csvPath;
		std::atomic<bool> m_stopRequested = false;
		std::unordered_map<std::int64_t, std::vector<STimingMetricAggregate>> m_pendingBuckets;
		std::vector<STimingMetricAggregate> m_overallMetricAggregates;
		std::thread m_thread;
	};
}
