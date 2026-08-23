#pragma once

namespace Foundation::Diagnostics
{
	class FTimingMetricsRuntime final : public FTlsCollectorRuntime
	{
	public:
		explicit FTimingMetricsRuntime(const STimingMetricsConfig& config);

		int GetFlushIntervalSeconds() const;
		std::size_t GetMetricCount() const;
		bool IsMetricIndexValid(FTimingMetricIndex metricIndex) const;
		std::string_view GetMetricName(FTimingMetricIndex metricIndex) const;
		std::optional<FTimingMetricIndex> FindMetricIndex(std::string_view metricName) const;

		void EnqueueSnapshot(std::unique_ptr<STimingSnapshot> snapshot);
		bool TryDequeueSnapshot(std::unique_ptr<STimingSnapshot>& outSnapshot);

	private:
		STimingMetricsConfig m_config;
		std::deque<std::unique_ptr<STimingSnapshot>> m_snapshotQueue;
		mutable std::mutex m_queueMutex;
	};
}
