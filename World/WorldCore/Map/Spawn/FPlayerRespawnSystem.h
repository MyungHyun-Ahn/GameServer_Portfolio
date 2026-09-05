#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;

	class FPlayerRespawnSystem final
	{
	public:
		[[nodiscard]] bool TrySelectSpawnPosition(FMapInstanceId mapInstanceId,
			FEntityId entityId,
			std::uint64_t lifeRevision,
			float collisionRadius,
			const SMapDefinition& mapDefinition,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			SVector2& outPosition,
			FSectorId& outSectorId,
			std::string& outError) const;
	};
}
