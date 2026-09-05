#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;
	struct SMapDefinition;
	struct SSectorTask;
	struct SSectorTaskOutput;

	class FSectorTaskProcessor final
	{
	public:
		[[nodiscard]] bool Execute(const SSectorTask& task,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			const SMapDefinition& mapDefinition,
			SSectorTaskOutput& outOutput,
			std::string& outError) const;
	};
}
