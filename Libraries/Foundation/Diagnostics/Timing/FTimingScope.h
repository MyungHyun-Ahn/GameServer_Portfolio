#pragma once

namespace Foundation::Diagnostics
{
	class FTimingThreadLocalCollector;

	class FTimingScope
	{
	public:
		FTimingScope(FTimingThreadLocalCollector& collector, FTimingMetricIndex metricIndex, std::uint64_t contextId = 0) noexcept;
		~FTimingScope() noexcept;

		FTimingScope(const FTimingScope&) = delete;
		FTimingScope& operator=(const FTimingScope&) = delete;

		void Stop() noexcept;
		void Cancel() noexcept;

	private:
		FTimingThreadLocalCollector* m_collector = nullptr;
		STimingPendingSample m_pendingSample{};
		bool m_isActive = true;
	};
}
