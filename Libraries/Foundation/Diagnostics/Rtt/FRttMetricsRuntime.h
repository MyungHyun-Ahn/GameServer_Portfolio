#pragma once

namespace Foundation::Diagnostics
{
	class FRttMetricsRuntime : public FTlsCollectorRuntime
	{
	public:
		explicit FRttMetricsRuntime(const SRttMetricsConfig& config);

		int GetFlushIntervalSeconds() const;
		std::size_t GetStageCount() const;
		bool IsStageIndexValid(FRttStageIndex stageIndex) const;
		std::string_view GetStageName(FRttStageIndex stageIndex) const;

		void EnqueueSnapshot(std::unique_ptr<SRttSnapshot> snapshot);
		bool TryDequeueSnapshot(std::unique_ptr<SRttSnapshot>& outSnapshot);

	private:
		SRttMetricsConfig m_config;
		std::deque<std::unique_ptr<SRttSnapshot>> m_snapshotQueue;
		mutable std::mutex m_queueMutex;
	};
}
