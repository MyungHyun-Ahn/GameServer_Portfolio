#pragma once

namespace WorldCore
{
	class FEntityRegistry;
	class FSectorGrid;

	class FMonsterSpawnSystem final
	{
	public:
		FMonsterSpawnSystem();
		~FMonsterSpawnSystem();

		FMonsterSpawnSystem(const FMonsterSpawnSystem&) = delete;
		FMonsterSpawnSystem& operator=(const FMonsterSpawnSystem&) = delete;

		[[nodiscard]] bool Configure(FMapInstanceId mapInstanceId,
			const SMapDefinition& mapDefinition,
			std::uint64_t fixedSeed,
			std::span<const SMonsterSpawnerRuntimeDefinition> definitions,
			std::string& outError);
		[[nodiscard]] bool IsConfigured() const noexcept;
		[[nodiscard]] bool CommitInitialAndDueSpawns(std::uint64_t committedTick,
			FEntityRegistry& entityRegistry,
			FSectorGrid& sectorGrid,
			std::vector<SMonsterSpawnResult>& outResults,
			std::string& outError);
		[[nodiscard]] bool CanScheduleRespawn(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			std::uint64_t deathTick,
			std::string& outError) const;
		[[nodiscard]] bool ScheduleRespawn(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			std::uint64_t deathTick,
			std::string& outError);
		[[nodiscard]] std::uint64_t GetStateHash() const noexcept;

	private:
		friend class FMapInstance;
		void InjectNextCommitFailureForTesting() noexcept;
		[[nodiscard]] bool RollbackScheduledRespawn(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			std::uint64_t deathTick) noexcept;

		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
