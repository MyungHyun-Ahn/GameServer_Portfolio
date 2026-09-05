#include "WorldCorePch.h"

#include "WorldCore/Entity/FMonsterEntity.h"

namespace WorldCore
{
	struct FMonsterEntity::SImpl final
	{
		FSpawnerDataId spawnerDataId = kInvalidSpawnerDataId;
		FSpawnGeneration spawnGeneration = kInvalidSpawnGeneration;
		SMonsterRuntimeSnapshot runtimeSnapshot{};
		std::uint32_t currentHp = 0;
		SVector2 spawnPosition{};
		FEntityId targetEntityId = kInvalidEntityId;
		EMonsterAiState aiState = EMonsterAiState::Idle;
		EMoveState moveState = EMoveState::Stop;
		std::uint64_t nextAttackTick = 0;
	};

	FMonsterEntity::FMonsterEntity(
		const FEntityId entityId,
		const FSpawnerDataId spawnerDataId,
		const FSpawnGeneration spawnGeneration,
		const SVector2& spawnPosition,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId,
		const SMonsterRuntimeSnapshot& runtimeSnapshot,
		const std::uint32_t currentHp)
		: FActorEntity(entityId, EActorKind::Monster, position, direction, sectorId)
		, m_impl(std::make_unique<SImpl>())
	{
		m_impl->spawnerDataId = spawnerDataId;
		m_impl->spawnGeneration = spawnGeneration;
		m_impl->runtimeSnapshot = runtimeSnapshot;
		m_impl->currentHp = currentHp;
		m_impl->spawnPosition = spawnPosition;
	}

	FMonsterEntity::~FMonsterEntity() = default;
	FMonsterEntity::FMonsterEntity(FMonsterEntity&& other) noexcept = default;
	FMonsterEntity& FMonsterEntity::operator=(FMonsterEntity&& other) noexcept = default;

	FMonsterDataId FMonsterEntity::GetMonsterDataId() const noexcept
	{
		return m_impl->runtimeSnapshot.monsterDataId;
	}

	FSpawnerDataId FMonsterEntity::GetSpawnerDataId() const noexcept
	{
		return m_impl->spawnerDataId;
	}

	FSpawnGeneration FMonsterEntity::GetSpawnGeneration() const noexcept
	{
		return m_impl->spawnGeneration;
	}

	const SMonsterRuntimeSnapshot& FMonsterEntity::GetRuntimeSnapshot() const noexcept
	{
		return m_impl->runtimeSnapshot;
	}

	std::uint32_t FMonsterEntity::GetCurrentHp() const noexcept
	{
		return m_impl->currentHp;
	}

	const SVector2& FMonsterEntity::GetSpawnPosition() const noexcept
	{
		return m_impl->spawnPosition;
	}

	FEntityId FMonsterEntity::GetTargetEntityId() const noexcept
	{
		return m_impl->targetEntityId;
	}

	EMonsterAiState FMonsterEntity::GetAiState() const noexcept
	{
		return m_impl->aiState;
	}

	EMoveState FMonsterEntity::GetMoveState() const noexcept
	{
		return m_impl->moveState;
	}

	std::uint64_t FMonsterEntity::GetNextAttackTick() const noexcept
	{
		return m_impl->nextAttackTick;
	}

	FMonsterEntity::SCommittedAiState FMonsterEntity::CaptureCommittedAiState() const noexcept
	{
		return {GetPosition(),
			GetDirection(),
			GetSectorId(),
			m_impl->targetEntityId,
			m_impl->aiState,
			m_impl->moveState,
			m_impl->nextAttackTick};
	}

	bool FMonsterEntity::RestoreCommittedAiState(
		const SCommittedAiState& state) noexcept
	{
		if (!ApplySpatialState(state.position, state.direction, state.sectorId))
		{
			return false;
		}

		m_impl->targetEntityId = state.targetEntityId;
		m_impl->aiState = state.aiState;
		m_impl->moveState = state.moveState;
		m_impl->nextAttackTick = state.nextAttackTick;
		return true;
	}

	bool FMonsterEntity::ApplyAiResult(
		const SMonsterAiResult& result) noexcept
	{
		if (result.entityId != GetEntityId() ||
			(result.aiState != EMonsterAiState::Idle && result.aiState != EMonsterAiState::Chase &&
				result.aiState != EMonsterAiState::AttackReady && result.aiState != EMonsterAiState::Return))
		{
			return false;
		}
		if ((result.aiState == EMonsterAiState::Idle && result.targetEntityId != kInvalidEntityId) ||
			((result.aiState == EMonsterAiState::Chase || result.aiState == EMonsterAiState::AttackReady) &&
				result.targetEntityId == kInvalidEntityId) ||
			(result.aiState == EMonsterAiState::Return && result.targetEntityId != kInvalidEntityId) ||
			(result.aiState == EMonsterAiState::Chase && result.moveState != EMoveState::Start) ||
			(result.aiState == EMonsterAiState::Return && result.moveState != EMoveState::Start) ||
			(result.aiState != EMonsterAiState::Chase && result.aiState != EMonsterAiState::Return &&
				result.moveState != EMoveState::Stop) ||
			!ApplySpatialState(result.acceptedPosition, result.direction, result.currentSectorId))
		{
			return false;
		}

		m_impl->targetEntityId = result.targetEntityId;
		m_impl->aiState = result.aiState;
		m_impl->moveState = result.moveState;
		return true;
	}

	bool FMonsterEntity::CommitAttackCooldown(
		const std::uint64_t currentTick) noexcept
	{
		const std::uint64_t cooldownTicks = m_impl->runtimeSnapshot.attackCooldownTicks;
		if (cooldownTicks == 0 || currentTick < m_impl->nextAttackTick ||
			currentTick > std::numeric_limits<std::uint64_t>::max() - cooldownTicks)
		{
			return false;
		}

		m_impl->nextAttackTick = currentTick + cooldownTicks;
		return true;
	}

	FMonsterEntity::SCommittedCombatState FMonsterEntity::CaptureCommittedCombatState() const noexcept
	{
		return {m_impl->currentHp};
	}

	bool FMonsterEntity::RestoreCommittedCombatState(
		const SCommittedCombatState& state) noexcept
	{
		if (state.currentHp == 0 || state.currentHp > m_impl->runtimeSnapshot.maxHp)
		{
			return false;
		}
		m_impl->currentHp = state.currentHp;
		return true;
	}

	bool FMonsterEntity::ApplyDamage(
		const std::uint32_t requestedDamage,
		std::uint32_t& outAppliedDamage) noexcept
	{
		outAppliedDamage = 0;
		if (requestedDamage == 0 || m_impl->currentHp == 0)
		{
			return false;
		}
		outAppliedDamage = std::min(requestedDamage, m_impl->currentHp);
		m_impl->currentHp -= outAppliedDamage;
		return true;
	}

	bool FMonsterEntity::AcquireAttacker(
		const FEntityId attackerEntityId,
		const SVector2& attackerPosition) noexcept
	{
		if (attackerEntityId == kInvalidEntityId || !IsFinite(attackerPosition) || m_impl->currentHp == 0 ||
			m_impl->runtimeSnapshot.aggroType != EMonsterAggroType::Passive)
		{
			return false;
		}

		m_impl->targetEntityId = attackerEntityId;
		const SVector2 delta{attackerPosition.x - GetPosition().x, attackerPosition.y - GetPosition().y};
		const SVector2 direction = NormalizeOrZero(delta);
		if (direction != SVector2{})
		{
			(void)ApplySpatialState(GetPosition(), direction, GetSectorId());
		}
		const float attackRange = m_impl->runtimeSnapshot.attackRange;
		if (GetDistanceSquared(GetPosition(), attackerPosition) <= attackRange * attackRange)
		{
			m_impl->aiState = EMonsterAiState::AttackReady;
			m_impl->moveState = EMoveState::Stop;
		}
		else
		{
			m_impl->aiState = EMonsterAiState::Chase;
			m_impl->moveState = EMoveState::Start;
		}
		return true;
	}
}
