#pragma once

namespace WorldCore
{
	class FSerialSectorExecutor final : public ISectorExecutor
	{
	public:
		[[nodiscard]] SSectorExecutionStartResult Execute(const SMapTickTicket& ticket,
			const FSectorTickPlan& tickPlan,
			const FSectorTaskProcessor& taskProcessor,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			const SMapDefinition& mapDefinition) override;
	};
}
