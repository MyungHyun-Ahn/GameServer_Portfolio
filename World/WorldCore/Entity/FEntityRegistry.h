#pragma once

namespace WorldCore
{
	class FActorEntity;
	class FMonsterEntity;
	class FPlayerEntity;
	struct SVector2;

	class FEntityRegistry final
	{
	public:
		FEntityRegistry();
		~FEntityRegistry();

		FEntityRegistry(const FEntityRegistry&) = delete;
		FEntityRegistry& operator=(const FEntityRegistry&) = delete;

		[[nodiscard]] bool AddPlayer(FEntityId entityId,
			FUserId userId,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId);
		[[nodiscard]] bool AddPlayer(FEntityId entityId,
			FUserId userId,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId,
			const SPlayerRuntimeSnapshot& runtimeSnapshot);
		[[nodiscard]] bool AddMonster(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId,
			const SMonsterRuntimeSnapshot& runtimeSnapshot,
			std::uint32_t currentHp);
		[[nodiscard]] bool AddMonster(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			const SVector2& spawnPosition,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId,
			const SMonsterRuntimeSnapshot& runtimeSnapshot,
			std::uint32_t currentHp);
		[[nodiscard]] bool RemoveActor(FEntityId entityId);
		[[nodiscard]] bool RemovePlayer(FEntityId entityId);
		[[nodiscard]] bool RemoveMonster(FEntityId entityId);
		[[nodiscard]] FActorEntity* FindActor(FEntityId entityId) noexcept;
		[[nodiscard]] const FActorEntity* FindActor(FEntityId entityId) const noexcept;
		[[nodiscard]] FPlayerEntity* FindPlayer(FEntityId entityId) noexcept;
		[[nodiscard]] const FPlayerEntity* FindPlayer(FEntityId entityId) const noexcept;
		[[nodiscard]] FMonsterEntity* FindMonster(FEntityId entityId) noexcept;
		[[nodiscard]] const FMonsterEntity* FindMonster(FEntityId entityId) const noexcept;
		[[nodiscard]] bool Contains(FEntityId entityId) const noexcept;
		[[nodiscard]] std::size_t GetActorCount() const noexcept;
		[[nodiscard]] std::size_t GetPlayerCount() const noexcept;
		[[nodiscard]] std::size_t GetMonsterCount() const noexcept;
		[[nodiscard]] std::vector<FEntityId> GetActorEntityIds() const;
		[[nodiscard]] std::vector<FEntityId> GetPlayerEntityIds() const;
		[[nodiscard]] std::vector<FEntityId> GetMonsterEntityIds() const;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
