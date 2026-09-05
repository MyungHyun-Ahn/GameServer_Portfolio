#pragma once

namespace WorldCore
{
	class FMapInstanceFactory;
	class FMapInstanceTestAccess;
	class FMonsterEntity;
	class FPlayerEntity;
	class FSectorGrid;
	class ISectorExecutor;

	class FMapInstance final
	{
	public:
		~FMapInstance();

		FMapInstance(const FMapInstance&) = delete;
		FMapInstance& operator=(const FMapInstance&) = delete;

		[[nodiscard]] FMapInstanceId GetMapInstanceId() const noexcept;
		[[nodiscard]] const SMapDefinition& GetDefinition() const noexcept;
		[[nodiscard]] std::size_t GetPlayerCount() const noexcept;
		[[nodiscard]] std::size_t GetMonsterCount() const noexcept;
		[[nodiscard]] const FPlayerEntity* FindPlayer(FEntityId entityId) const noexcept;
		[[nodiscard]] const FMonsterEntity* FindMonster(FEntityId entityId) const noexcept;
		[[nodiscard]] const FSectorGrid& GetSectorGrid() const noexcept;
		[[nodiscard]] std::size_t GetPendingMoveCount() const noexcept;
		[[nodiscard]] std::size_t GetPendingAttackCount() const noexcept;
		[[nodiscard]] std::uint64_t GetTickIndex() const noexcept;
		[[nodiscard]] EMapTickExecutionState GetTickExecutionState() const noexcept;
		[[nodiscard]] SMapTickTicket GetActiveTickTicket() const noexcept;
		[[nodiscard]] std::uint64_t GetStateHash() const;
		[[nodiscard]] bool ConfigureMonsterSpawning(std::uint64_t fixedSeed,
			std::span<const SMonsterSpawnerRuntimeDefinition> definitions,
			std::string& outError);

		[[nodiscard]] bool AddPlayer(FEntityId entityId,
			FUserId userId,
			const SVector2& position,
			const SVector2& direction,
			std::vector<SVisibilityEvent>& outVisibilityEvents,
			std::string& outError);
		[[nodiscard]] bool AddPlayer(FEntityId entityId,
			FUserId userId,
			const SVector2& position,
			const SVector2& direction,
			const SPlayerRuntimeSnapshot& runtimeSnapshot,
			std::vector<SVisibilityEvent>& outVisibilityEvents,
			std::string& outError);
		[[nodiscard]] bool AddPlayerAtRandomSpawn(FEntityId entityId,
			FUserId userId,
			const SVector2& direction,
			std::vector<SVisibilityEvent>& outVisibilityEvents,
			std::string& outError);
		[[nodiscard]] bool AddPlayerAtRandomSpawn(FEntityId entityId,
			FUserId userId,
			const SVector2& direction,
			const SPlayerRuntimeSnapshot& runtimeSnapshot,
			std::vector<SVisibilityEvent>& outVisibilityEvents,
			std::string& outError);
		[[nodiscard]] bool RemovePlayer(FEntityId entityId, std::vector<SVisibilityEvent>& outVisibilityEvents, std::string& outError);
		[[nodiscard]] bool RemoveMonster(FEntityId entityId, std::vector<SVisibilityEvent>& outVisibilityEvents, std::string& outError);
		[[nodiscard]] bool ApplyPlayerRuntimeSnapshot(FEntityId entityId,
			const SPlayerRuntimeSnapshot& runtimeSnapshot,
			std::string& outError);
		[[nodiscard]] bool QueueMove(const SMoveCommand& command);
		[[nodiscard]] bool QueuePlayerAttack(const SPlayerAttackCommand& command);
		[[nodiscard]] EMapTickCompletionResult CompleteTickExecution(SMapTickExecutionCompletion completion);
		[[nodiscard]] SMapTickResult Tick();

	private:
		friend class FMapInstanceFactory;
		friend class FMapInstanceTestAccess;
		FMapInstance(FMapInstanceId mapInstanceId,
			FMapIncarnation mapIncarnation,
			const SMapDefinition& definition,
			std::unique_ptr<ISectorExecutor> sectorExecutor);
		[[nodiscard]] SMapTickResult StartTickExecution();
		[[nodiscard]] SMapTickResult FinalizeTickExecution();
		void InjectNextMonsterSpawnCommitFailureForTesting() noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
