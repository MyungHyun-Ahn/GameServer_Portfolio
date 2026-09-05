#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;
	class FSectorTaskProcessor;
	class FSectorTickPlan;
}

namespace WorldServer::Contents
{
	using FTaskGraphCompletionHandler = std::function<void(WorldCore::SMapTickExecutionCompletion)>;

	struct STaskGraphSectorExecutionStats final
	{
		std::uint64_t startedExecutionCount = 0;
		std::uint64_t completedExecutionCount = 0;
		std::uint64_t failedExecutionCount = 0;
		std::uint64_t canceledExecutionCount = 0;
		std::uint64_t executedTaskCount = 0;
		std::uint32_t activeTaskCount = 0;
		std::uint32_t maxParallelTaskCount = 0;
		std::uint64_t workerMask = 0;
	};

	class FTaskGraphSectorExecutionService final
	{
	public:
		using FCompletionHandler = FTaskGraphCompletionHandler;

		FTaskGraphSectorExecutionService(std::shared_ptr<Foundation::ILogger> logger,
			std::vector<ContentsRuntime::Core::FContentInstanceId> executorInstanceIds,
			std::uint32_t pumpBatchSize);
		~FTaskGraphSectorExecutionService();

		FTaskGraphSectorExecutionService(const FTaskGraphSectorExecutionService&) = delete;
		FTaskGraphSectorExecutionService& operator=(const FTaskGraphSectorExecutionService&) = delete;

		void BindBridge(ContentsRuntime::Bridge::IContentBridge& bridge) noexcept;
		void BeginShutdown() noexcept;
		void UnbindBridge(ContentsRuntime::Bridge::IContentBridge& bridge) noexcept;
		void DrainOwnerCallbacks(ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId);
		[[nodiscard]] bool IsStopping() const noexcept;
		[[nodiscard]] WorldCore::SSectorExecutionStartResult BeginExecution(
			ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId,
			const WorldCore::SMapTickTicket& ticket,
			const WorldCore::FSectorTickPlan& tickPlan,
			const WorldCore::FSectorTaskProcessor& taskProcessor,
			const WorldCore::FEntityRegistry& entityRegistry,
			const WorldCore::FSectorGrid& sectorGrid,
			const WorldCore::SMapDefinition& mapDefinition,
			FCompletionHandler completionHandler);
		[[nodiscard]] std::size_t GetExecutorInstanceCount() const noexcept;
		[[nodiscard]] STaskGraphSectorExecutionStats GetStatsSnapshot() const noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
