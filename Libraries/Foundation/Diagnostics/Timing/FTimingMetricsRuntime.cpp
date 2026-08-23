#include "FoundationPch.h"

#include "FTimingMetricsRuntime.h"

namespace Foundation::Diagnostics
{
	FTimingMetricsRuntime::FTimingMetricsRuntime(
		const STimingMetricsConfig& config)
		: m_config(config)
	{
		if (m_config.flushIntervalSeconds <= 0)
		{
			m_config.flushIntervalSeconds = 60;
		}

		if (m_config.metricNames.size() > static_cast<std::size_t>(std::numeric_limits<FTimingMetricIndex>::max()) + 1)
		{
			throw std::invalid_argument("Timing metric count exceeds FTimingMetricIndex capacity.");
		}
	}

	int FTimingMetricsRuntime::GetFlushIntervalSeconds() const
	{
		return m_config.flushIntervalSeconds;
	}

	std::size_t FTimingMetricsRuntime::GetMetricCount() const
	{
		return m_config.metricNames.size();
	}

	bool FTimingMetricsRuntime::IsMetricIndexValid(
		const FTimingMetricIndex metricIndex) const
	{
		return static_cast<std::size_t>(metricIndex) < m_config.metricNames.size();
	}

	std::string_view FTimingMetricsRuntime::GetMetricName(
		const FTimingMetricIndex metricIndex) const
	{
		if (!IsMetricIndexValid(metricIndex))
		{
			return {};
		}

		return m_config.metricNames[static_cast<std::size_t>(metricIndex)];
	}

	std::optional<FTimingMetricIndex> FTimingMetricsRuntime::FindMetricIndex(
		const std::string_view metricName) const
	{
		const auto metricIt = std::find(m_config.metricNames.begin(), m_config.metricNames.end(), metricName);
		if (metricIt == m_config.metricNames.end())
		{
			return std::nullopt;
		}

		return static_cast<FTimingMetricIndex>(std::distance(m_config.metricNames.begin(), metricIt));
	}

	void FTimingMetricsRuntime::EnqueueSnapshot(
		std::unique_ptr<STimingSnapshot> snapshot)
	{
		if (snapshot == nullptr)
		{
			return;
		}

		const std::lock_guard<std::mutex> lock(m_queueMutex);
		m_snapshotQueue.emplace_back(std::move(snapshot));
	}

	bool FTimingMetricsRuntime::TryDequeueSnapshot(
		std::unique_ptr<STimingSnapshot>& outSnapshot)
	{
		const std::lock_guard<std::mutex> lock(m_queueMutex);
		if (m_snapshotQueue.empty())
		{
			return false;
		}

		outSnapshot = std::move(m_snapshotQueue.front());
		m_snapshotQueue.pop_front();
		return true;
	}
}
