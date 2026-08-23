#pragma once

namespace Foundation::Diagnostics
{
	class FRttMetricsRuntime;

	class FRttCsvLogger
	{
	public:
		FRttCsvLogger(FRttMetricsRuntime& rttMetricsRuntime, std::string csvPath);
		~FRttCsvLogger();

		FRttCsvLogger(const FRttCsvLogger&) = delete;
		FRttCsvLogger& operator=(const FRttCsvLogger&) = delete;

		void Start();
		void Stop();

	private:
		void Run();

		FRttMetricsRuntime& m_rttMetricsRuntime;
		std::string m_csvPath;
		std::atomic<bool> m_stopRequested = false;
		std::unordered_map<std::int64_t, std::vector<SRttStageAggregate>> m_pendingBuckets;
		std::vector<SRttStageAggregate> m_overallStageAggregates;
		std::thread m_thread;
	};
}
