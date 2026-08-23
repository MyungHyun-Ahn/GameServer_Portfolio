#pragma once

namespace Foundation::Diagnostics
{
	class FRttMetricsRuntime;

	class FRttThreadLocalCollector
	{
	public:
		explicit FRttThreadLocalCollector(FRttMetricsRuntime* rttMetricsRuntime);
		~FRttThreadLocalCollector();

		FRttThreadLocalCollector(const FRttThreadLocalCollector&) = delete;
		FRttThreadLocalCollector& operator=(const FRttThreadLocalCollector&) = delete;

		SRttPendingRequest BeginRequest(FRttStageIndex stageIndex, int sessionIndex) const;
		void RecordSample(const SRttPendingRequest& pendingRequest, std::chrono::system_clock::time_point receivedSystem) const;
		void RecordTimeout(FRttStageIndex stageIndex, std::chrono::system_clock::time_point timeoutSystem) const;

	private:
		FRttMetricsRuntime* m_rttMetricsRuntime = nullptr;
	};
}
