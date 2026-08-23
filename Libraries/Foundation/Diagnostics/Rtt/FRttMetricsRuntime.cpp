#include "FoundationPch.h"

#include "FRttMetricsRuntime.h"

namespace Foundation::Diagnostics
{
	FRttMetricsRuntime::FRttMetricsRuntime(
		const SRttMetricsConfig& config)
		: m_config(config)
	{
		if (m_config.flushIntervalSeconds <= 0)
		{
			m_config.flushIntervalSeconds = 60;
		}
	}

	int FRttMetricsRuntime::GetFlushIntervalSeconds() const
	{
		return m_config.flushIntervalSeconds;
	}

	std::size_t FRttMetricsRuntime::GetStageCount() const
	{
		return m_config.stageNames.size();
	}

	bool FRttMetricsRuntime::IsStageIndexValid(
		const FRttStageIndex stageIndex) const
	{
		return static_cast<std::size_t>(stageIndex) < m_config.stageNames.size();
	}

	std::string_view FRttMetricsRuntime::GetStageName(
		const FRttStageIndex stageIndex) const
	{
		if (!IsStageIndexValid(stageIndex))
		{
			return {};
		}

		return m_config.stageNames[static_cast<std::size_t>(stageIndex)];
	}

	void FRttMetricsRuntime::EnqueueSnapshot(
		std::unique_ptr<SRttSnapshot> snapshot)
	{
		if (snapshot == nullptr)
		{
			return;
		}

		const std::lock_guard<std::mutex> lock(m_queueMutex);
		m_snapshotQueue.emplace_back(std::move(snapshot));
	}

	bool FRttMetricsRuntime::TryDequeueSnapshot(
		std::unique_ptr<SRttSnapshot>& outSnapshot)
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
