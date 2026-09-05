#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;
	struct SMapDefinition;
	struct SSectorTask;

	struct SSectorUpdateContext final
	{
		const SSectorTask& task;
		const FEntityRegistry& entityRegistry;
		const FSectorGrid& sectorGrid;
		const SMapDefinition& mapDefinition;
	};
}
