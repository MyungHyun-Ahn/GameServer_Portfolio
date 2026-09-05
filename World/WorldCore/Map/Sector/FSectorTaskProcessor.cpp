#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSectorTaskProcessor.h"
#include "WorldCore/Map/Sector/FSectorUpdateContext.h"
#include "WorldCore/Map/Sector/FSectorUpdatePipeline.h"

namespace WorldCore
{
	bool FSectorTaskProcessor::Execute(
		const SSectorTask& task,
		const FEntityRegistry& entityRegistry,
		const FSectorGrid& sectorGrid,
		const SMapDefinition& mapDefinition,
		SSectorTaskOutput& outOutput,
		std::string& outError) const
	{
		const SSectorUpdateContext context{task, entityRegistry, sectorGrid, mapDefinition};
		const FSectorUpdatePipeline updatePipeline;
		return updatePipeline.Update(context, outOutput, outError);
	}
}
