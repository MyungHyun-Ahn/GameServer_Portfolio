#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;

	class FMapVisibilitySystem final
	{
	public:
		FMapVisibilitySystem();
		~FMapVisibilitySystem();

		FMapVisibilitySystem(const FMapVisibilitySystem&) = delete;
		FMapVisibilitySystem& operator=(const FMapVisibilitySystem&) = delete;

		[[nodiscard]] std::vector<SVisibilityEvent> Refresh(const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			std::uint32_t visibilitySectorRadius,
			std::span<const FEntityId> movedEntityIds);
		[[nodiscard]] std::vector<SActorAttackEvent> BuildActorAttackEvents(const FEntityRegistry& entityRegistry,
			std::span<const SActorAttackResult> attackResults) const;
		[[nodiscard]] std::vector<SActorDeathEvent> BuildActorDeathEvents(const FEntityRegistry& entityRegistry,
			std::span<const SActorDeathResult> deathResults) const;
		[[nodiscard]] std::vector<SActorRespawnEvent> BuildActorRespawnEvents(const FEntityRegistry& entityRegistry,
			std::span<const SActorRespawnResult> respawnResults) const;
		void Reset();

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
