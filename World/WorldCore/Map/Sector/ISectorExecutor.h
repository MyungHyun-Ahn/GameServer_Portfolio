#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;
	class FSectorTickPlan;
	class FSectorTaskProcessor;
	struct SSectorTaskOutput;

	class ISectorExecutor
	{
	public:
		virtual ~ISectorExecutor() = default;

		[[nodiscard]] virtual SSectorExecutionStartResult Execute(const SMapTickTicket& ticket,
			const FSectorTickPlan& tickPlan,
			const FSectorTaskProcessor& taskProcessor,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			const SMapDefinition& mapDefinition) = 0;
	};
}
