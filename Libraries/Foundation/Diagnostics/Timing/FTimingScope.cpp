#include "FoundationPch.h"

#include "FTimingScope.h"

#include "FTimingThreadLocalCollector.h"

namespace Foundation::Diagnostics
{
	FTimingScope::FTimingScope(
		FTimingThreadLocalCollector& collector,
		const FTimingMetricIndex metricIndex,
		const std::uint64_t contextId) noexcept
		: m_collector(&collector)
		, m_pendingSample(collector.BeginSample(metricIndex, contextId))
	{
	}

	FTimingScope::~FTimingScope() noexcept
	{
		Stop();
	}

	void FTimingScope::Stop() noexcept
	{
		if (!m_isActive || m_collector == nullptr)
		{
			return;
		}

		m_isActive = false;
		m_collector->RecordSample(m_pendingSample);
	}

	void FTimingScope::Cancel() noexcept
	{
		m_isActive = false;
	}
}
