#pragma once

namespace ContentsRuntime::Core
{
	class IContent;
	struct SContentExecutionState;
}

namespace ContentsRuntime::Threading
{
	class FContentThread
	{
	public:
		FContentThread(Bridge::IContentBridge& bridge, const Core::SContentRuntimeConfig& config, std::uint32_t workerIndex);
		~FContentThread();

		bool RegisterContent(Core::SContentExecutionState& executionState);
		bool DetachContent(Core::FContentInstanceId contentInstanceId);
		bool DetachContentForTransfer(Core::SContentExecutionState& executionState);
		void Start();
		void Stop();

		Core::SContentThreadStats GetStatsSnapshot(Core::FContentInstanceId contentInstanceId);
		std::uint32_t GetWorkerIndex() const noexcept;
		static std::uint32_t GetCurrentWorkerIndex() noexcept;
		std::uint64_t GetApproxPendingWorkCount() const noexcept;

		bool EnqueueEnter(Core::SContentLifecycleEvent event);
		bool EnqueueLeave(Core::SContentLifecycleEvent event);
		bool EnqueuePacket(Core::FOwnedPacketEnvelope packet);
		Core::EPacketEnqueueResult EnqueuePacketDetailed(Core::FOwnedPacketEnvelope packet);
		bool EnqueueCompletion(Core::FContentInstanceId contentInstanceId, std::function<void()> completion);

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
