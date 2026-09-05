#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSerialSectorExecutor.h"
#include "WorldCore/Map/Sector/FSectorTaskProcessor.h"
#include "WorldCore/Map/Sector/FSectorTickPlan.h"

namespace WorldCore
{
	SSectorExecutionStartResult FSerialSectorExecutor::Execute(
		const SMapTickTicket& ticket,
		const FSectorTickPlan& tickPlan,
		const FSectorTaskProcessor& taskProcessor,
		const FEntityRegistry& entityRegistry,
		const FSectorGrid& sectorGrid,
		const SMapDefinition& mapDefinition)
	{
		SSectorExecutionStartResult result{};
		if (ticket.tickIndex != tickPlan.GetTickIndex() || tickPlan.GetTaskCount() == 0 ||
			tickPlan.GetWaves().size() != kSectorTaskWaveCount)
		{
			result.failureReason = "Serial Sector execution requires a matching valid Tick Plan.";
			return result;
		}

		result.taskOutputs.reserve(tickPlan.GetTaskCount());
		for (const SSectorTaskWave& wave : tickPlan.GetWaves())
		{
			for (const SSectorTask& task : wave.tasks)
			{
				SSectorTaskOutput output{};
				if (!taskProcessor.Execute(task, entityRegistry, sectorGrid, mapDefinition, output, result.failureReason))
				{
					result.taskOutputs.clear();
					return result;
				}
				result.taskOutputs.push_back(std::move(output));
			}
		}

		if (result.taskOutputs.size() != tickPlan.GetTaskCount())
		{
			result.taskOutputs.clear();
			result.failureReason = "Serial Sector execution did not process every Tick Plan Task.";
			return result;
		}

		std::sort(result.taskOutputs.begin(),
			result.taskOutputs.end(),
			[](const SSectorTaskOutput& lhs, const SSectorTaskOutput& rhs)
			{
				return lhs.stableOrder < rhs.stableOrder;
			});
		result.executionResult = ESectorExecutionResult::CompletedInline;
		return result;
	}
}
