#include "WorldServerPch.h"

#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutionService.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutor.h"

namespace WorldServer::Contents
{
	FTaskGraphSectorExecutor::FTaskGraphSectorExecutor(
		std::shared_ptr<FTaskGraphSectorExecutionService> executionService,
		const ContentsRuntime::Core::FContentInstanceId ownerContentInstanceId,
		FTaskGraphSectorExecutionService::FCompletionHandler completionHandler)
		: m_executionService(std::move(executionService))
		, m_ownerContentInstanceId(ownerContentInstanceId)
		, m_completionHandler(std::move(completionHandler))
	{
	}

	WorldCore::SSectorExecutionStartResult FTaskGraphSectorExecutor::Execute(
		const WorldCore::SMapTickTicket& ticket,
		const WorldCore::FSectorTickPlan& tickPlan,
		const WorldCore::FSectorTaskProcessor& taskProcessor,
		const WorldCore::FEntityRegistry& entityRegistry,
		const WorldCore::FSectorGrid& sectorGrid,
		const WorldCore::SMapDefinition& mapDefinition)
	{
		if (m_executionService == nullptr)
		{
			return {WorldCore::ESectorExecutionResult::Failed, {}, "TaskGraph execution service is unavailable."};
		}

		return m_executionService->BeginExecution(
			m_ownerContentInstanceId, ticket, tickPlan, taskProcessor, entityRegistry, sectorGrid, mapDefinition, m_completionHandler);
	}
}
