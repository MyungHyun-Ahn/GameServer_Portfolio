#pragma once

namespace WorldServer::Contents
{
	class FTaskGraphSectorExecutionService;

	class FTaskGraphSectorExecutor final : public WorldCore::ISectorExecutor
	{
	public:
		FTaskGraphSectorExecutor(std::shared_ptr<FTaskGraphSectorExecutionService> executionService,
			ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId,
			std::function<void(WorldCore::SMapTickExecutionCompletion)> completionHandler);

		[[nodiscard]] WorldCore::SSectorExecutionStartResult Execute(const WorldCore::SMapTickTicket& ticket,
			const WorldCore::FSectorTickPlan& tickPlan,
			const WorldCore::FSectorTaskProcessor& taskProcessor,
			const WorldCore::FEntityRegistry& entityRegistry,
			const WorldCore::FSectorGrid& sectorGrid,
			const WorldCore::SMapDefinition& mapDefinition) override;

	private:
		std::shared_ptr<FTaskGraphSectorExecutionService> m_executionService;
		ContentsRuntime::Core::FContentInstanceId m_ownerContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::function<void(WorldCore::SMapTickExecutionCompletion)> m_completionHandler;
	};
}
