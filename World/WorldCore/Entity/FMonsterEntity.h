#pragma once

namespace WorldCore
{
	class FMapInstance;
	struct SMonsterAiResult;
	struct SVector2;

	class FMonsterEntity final : public FActorEntity
	{
	public:
		FMonsterEntity(FEntityId entityId,
			FSpawnerDataId spawnerDataId,
			FSpawnGeneration spawnGeneration,
			const SVector2& spawnPosition,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId,
			const SMonsterRuntimeSnapshot& runtimeSnapshot,
			std::uint32_t currentHp);
		~FMonsterEntity() override;

		FMonsterEntity(const FMonsterEntity&) = delete;
		FMonsterEntity& operator=(const FMonsterEntity&) = delete;
		FMonsterEntity(FMonsterEntity&& other) noexcept;
		FMonsterEntity& operator=(FMonsterEntity&& other) noexcept;

		[[nodiscard]] FMonsterDataId GetMonsterDataId() const noexcept;
		[[nodiscard]] FSpawnerDataId GetSpawnerDataId() const noexcept;
		[[nodiscard]] FSpawnGeneration GetSpawnGeneration() const noexcept;
		[[nodiscard]] const SMonsterRuntimeSnapshot& GetRuntimeSnapshot() const noexcept;
		[[nodiscard]] std::uint32_t GetCurrentHp() const noexcept;
		[[nodiscard]] const SVector2& GetSpawnPosition() const noexcept;
		[[nodiscard]] FEntityId GetTargetEntityId() const noexcept;
		[[nodiscard]] EMonsterAiState GetAiState() const noexcept;
		[[nodiscard]] EMoveState GetMoveState() const noexcept;
		[[nodiscard]] std::uint64_t GetNextAttackTick() const noexcept;

	private:
		friend class FMapInstance;

		struct SCommittedAiState final
		{
			SVector2 position{};
			SVector2 direction{};
			FSectorId sectorId = kInvalidSectorId;
			FEntityId targetEntityId = kInvalidEntityId;
			EMonsterAiState aiState = EMonsterAiState::Idle;
			EMoveState moveState = EMoveState::Stop;
			std::uint64_t nextAttackTick = 0;
		};

		struct SCommittedCombatState final
		{
			std::uint32_t currentHp = 0;
		};

		[[nodiscard]] SCommittedAiState CaptureCommittedAiState() const noexcept;
		[[nodiscard]] bool RestoreCommittedAiState(const SCommittedAiState& state) noexcept;
		[[nodiscard]] bool ApplyAiResult(const SMonsterAiResult& result) noexcept;
		[[nodiscard]] bool CommitAttackCooldown(std::uint64_t currentTick) noexcept;
		[[nodiscard]] SCommittedCombatState CaptureCommittedCombatState() const noexcept;
		[[nodiscard]] bool RestoreCommittedCombatState(const SCommittedCombatState& state) noexcept;
		[[nodiscard]] bool ApplyDamage(std::uint32_t requestedDamage, std::uint32_t& outAppliedDamage) noexcept;
		[[nodiscard]] bool AcquireAttacker(FEntityId attackerEntityId, const SVector2& attackerPosition) noexcept;

		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
