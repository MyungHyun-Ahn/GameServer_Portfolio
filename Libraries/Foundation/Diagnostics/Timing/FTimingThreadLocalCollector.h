#pragma once

namespace Foundation::Diagnostics
{
	class FTimingMetricsRuntime;

	class FTimingThreadLocalCollector
	{
	public:
		explicit FTimingThreadLocalCollector(FTimingMetricsRuntime* timingMetricsRuntime);
		~FTimingThreadLocalCollector();

		FTimingThreadLocalCollector(const FTimingThreadLocalCollector&) = delete;
		FTimingThreadLocalCollector& operator=(const FTimingThreadLocalCollector&) = delete;

		STimingPendingSample BeginSample(FTimingMetricIndex metricIndex, std::uint64_t contextId = 0) const noexcept;
		void RecordSample(const STimingPendingSample& pendingSample,
			std::chrono::system_clock::time_point finishedSystem = std::chrono::system_clock::now()) const noexcept;
		void RecordDuration(FTimingMetricIndex metricIndex, std::chrono::nanoseconds elapsed, std::uint64_t contextId = 0) const noexcept;

	private:
		void RecordDurationInternal(FTimingMetricIndex metricIndex,
			std::chrono::nanoseconds elapsed,
			std::uint64_t contextId,
			std::chrono::system_clock::time_point startedSystem,
			std::chrono::system_clock::time_point finishedSystem) const noexcept;

		FTimingMetricsRuntime* m_timingMetricsRuntime = nullptr;
	};
}
