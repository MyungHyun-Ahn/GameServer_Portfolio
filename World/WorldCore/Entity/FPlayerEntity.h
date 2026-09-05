#pragma once

namespace WorldCore
{
	class FMapInstance;
	struct SMoveResult;
	struct SVector2;

	class FPlayerEntity final : public FActorEntity
	{
	public:
		FPlayerEntity(FEntityId entityId, FUserId userId, const SVector2& position, const SVector2& direction, FSectorId sectorId);
		FPlayerEntity(FEntityId entityId,
			FUserId userId,
			const SVector2& position,
			const SVector2& direction,
			FSectorId sectorId,
			const SPlayerRuntimeSnapshot& runtimeSnapshot);
		~FPlayerEntity() override;

		FPlayerEntity(const FPlayerEntity&) = delete;
		FPlayerEntity& operator=(const FPlayerEntity&) = delete;
		FPlayerEntity(FPlayerEntity&& other) noexcept;
		FPlayerEntity& operator=(FPlayerEntity&& other) noexcept;

		[[nodiscard]] FEntityId GetEntityId() const noexcept;
		[[nodiscard]] FUserId GetUserId() const noexcept;
		[[nodiscard]] const SVector2& GetPosition() const noexcept;
		[[nodiscard]] const SVector2& GetDirection() const noexcept;
		[[nodiscard]] FSectorId GetSectorId() const noexcept;
		[[nodiscard]] FMoveSequence GetLastMoveSequence() const noexcept;
		[[nodiscard]] EMoveState GetMoveState() const noexcept;
		[[nodiscard]] bool HasRuntimeSnapshot() const noexcept;
		[[nodiscard]] const SPlayerRuntimeSnapshot& GetRuntimeSnapshot() const noexcept;
		[[nodiscard]] std::uint32_t GetCurrentHp() const noexcept;
		[[nodiscard]] std::uint32_t GetCurrentMp() const noexcept;
		[[nodiscard]] EPlayerLifeState GetLifeState() const noexcept;
		[[nodiscard]] bool IsAlive() const noexcept;
		[[nodiscard]] std::uint64_t GetLifeRevision() const noexcept;
		[[nodiscard]] std::uint64_t GetRespawnDueTick() const noexcept;
		[[nodiscard]] FEntityId GetLastKillerEntityId() const noexcept;
		[[nodiscard]] std::uint64_t GetNextBasicAttackTick() const noexcept;
		[[nodiscard]] bool ApplyRuntimeSnapshot(const SPlayerRuntimeSnapshot& runtimeSnapshot);
		[[nodiscard]] bool ApplyMove(const SMoveResult& moveResult) noexcept;

	private:
		friend class FMapInstance;

		struct SCommittedMoveState final
		{
			SVector2 position{};
			SVector2 direction{};
			FSectorId sectorId = kInvalidSectorId;
			FMoveSequence lastMoveSequence = 0;
			EMoveState moveState = EMoveState::Stop;
		};

		struct SCommittedCombatState final
		{
			std::uint32_t currentHp = 0;
			std::uint32_t currentMp = 0;
			std::uint64_t nextBasicAttackTick = 0;
		};

		struct SCommittedLifecycleState final
		{
			SCommittedMoveState move{};
			SCommittedCombatState combat{};
			EPlayerLifeState lifeState = EPlayerLifeState::Alive;
			std::uint64_t lifeRevision = 1;
			std::uint64_t respawnDueTick = 0;
			FEntityId lastKillerEntityId = kInvalidEntityId;
		};

		[[nodiscard]] SCommittedMoveState CaptureCommittedMoveState() const noexcept;
		[[nodiscard]] bool RestoreCommittedMoveState(const SCommittedMoveState& state) noexcept;
		[[nodiscard]] SCommittedCombatState CaptureCommittedCombatState() const noexcept;
		[[nodiscard]] bool RestoreCommittedCombatState(const SCommittedCombatState& state) noexcept;
		[[nodiscard]] SCommittedLifecycleState CaptureCommittedLifecycleState() const noexcept;
		[[nodiscard]] bool RestoreCommittedLifecycleState(const SCommittedLifecycleState& state) noexcept;
		[[nodiscard]] bool ApplyDamage(std::uint32_t requestedDamage, std::uint32_t& outAppliedDamage) noexcept;
		[[nodiscard]] bool CommitBasicAttackCooldown(std::uint64_t currentTick, std::uint64_t cooldownTicks) noexcept;
		[[nodiscard]] bool CommitDeath(FEntityId killerEntityId, std::uint64_t currentTick, std::uint64_t respawnDelayTicks) noexcept;
		[[nodiscard]] bool CommitRespawn(const SVector2& position, const SVector2& direction, FSectorId sectorId) noexcept;

		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
