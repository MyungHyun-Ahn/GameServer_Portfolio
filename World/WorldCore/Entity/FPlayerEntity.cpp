#include "WorldCorePch.h"

#include "WorldCore/Entity/FPlayerEntity.h"

namespace WorldCore
{
	struct FPlayerEntity::SImpl final
	{
		FUserId userId = kInvalidUserId;
		FMoveSequence lastMoveSequence = 0;
		EMoveState moveState = EMoveState::Stop;
		bool hasRuntimeSnapshot = false;
		SPlayerRuntimeSnapshot runtimeSnapshot{};
		std::uint32_t currentHp = 0;
		std::uint32_t currentMp = 0;
		EPlayerLifeState lifeState = EPlayerLifeState::Alive;
		std::uint64_t lifeRevision = 1;
		std::uint64_t respawnDueTick = 0;
		FEntityId lastKillerEntityId = kInvalidEntityId;
		std::uint64_t nextBasicAttackTick = 0;
	};

	FPlayerEntity::FPlayerEntity(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId)
		: FActorEntity(entityId, EActorKind::Player, position, direction, sectorId)
		, m_impl(std::make_unique<SImpl>())
	{
		m_impl->userId = userId;
	}

	FPlayerEntity::FPlayerEntity(
		const FEntityId entityId,
		const FUserId userId,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId,
		const SPlayerRuntimeSnapshot& runtimeSnapshot)
		: FPlayerEntity(entityId, userId, position, direction, sectorId)
	{
		m_impl->hasRuntimeSnapshot = true;
		m_impl->runtimeSnapshot = runtimeSnapshot;
		m_impl->currentHp = runtimeSnapshot.maxHp;
		m_impl->currentMp = runtimeSnapshot.maxMp;
	}

	FPlayerEntity::~FPlayerEntity() = default;
	FPlayerEntity::FPlayerEntity(FPlayerEntity&& other) noexcept = default;
	FPlayerEntity& FPlayerEntity::operator=(FPlayerEntity&& other) noexcept = default;

	FEntityId FPlayerEntity::GetEntityId() const noexcept
	{
		return FActorEntity::GetEntityId();
	}

	FUserId FPlayerEntity::GetUserId() const noexcept
	{
		return m_impl->userId;
	}

	const SVector2& FPlayerEntity::GetPosition() const noexcept
	{
		return FActorEntity::GetPosition();
	}

	const SVector2& FPlayerEntity::GetDirection() const noexcept
	{
		return FActorEntity::GetDirection();
	}

	FSectorId FPlayerEntity::GetSectorId() const noexcept
	{
		return FActorEntity::GetSectorId();
	}

	FMoveSequence FPlayerEntity::GetLastMoveSequence() const noexcept
	{
		return m_impl->lastMoveSequence;
	}

	EMoveState FPlayerEntity::GetMoveState() const noexcept
	{
		return m_impl->moveState;
	}

	bool FPlayerEntity::HasRuntimeSnapshot() const noexcept
	{
		return m_impl->hasRuntimeSnapshot;
	}

	const SPlayerRuntimeSnapshot& FPlayerEntity::GetRuntimeSnapshot() const noexcept
	{
		return m_impl->runtimeSnapshot;
	}

	std::uint32_t FPlayerEntity::GetCurrentHp() const noexcept
	{
		return m_impl->currentHp;
	}

	std::uint32_t FPlayerEntity::GetCurrentMp() const noexcept
	{
		return m_impl->currentMp;
	}

	EPlayerLifeState FPlayerEntity::GetLifeState() const noexcept
	{
		return m_impl->lifeState;
	}

	bool FPlayerEntity::IsAlive() const noexcept
	{
		return m_impl->lifeState == EPlayerLifeState::Alive;
	}

	std::uint64_t FPlayerEntity::GetLifeRevision() const noexcept
	{
		return m_impl->lifeRevision;
	}

	std::uint64_t FPlayerEntity::GetRespawnDueTick() const noexcept
	{
		return m_impl->respawnDueTick;
	}

	FEntityId FPlayerEntity::GetLastKillerEntityId() const noexcept
	{
		return m_impl->lastKillerEntityId;
	}

	std::uint64_t FPlayerEntity::GetNextBasicAttackTick() const noexcept
	{
		return m_impl->nextBasicAttackTick;
	}

	bool FPlayerEntity::ApplyRuntimeSnapshot(
		const SPlayerRuntimeSnapshot& runtimeSnapshot)
	{
		std::string snapshotError;
		if (!IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}
		if (!m_impl->hasRuntimeSnapshot)
		{
			m_impl->hasRuntimeSnapshot = true;
			m_impl->runtimeSnapshot = runtimeSnapshot;
			m_impl->currentHp = runtimeSnapshot.maxHp;
			m_impl->currentMp = runtimeSnapshot.maxMp;
			return true;
		}
		if (runtimeSnapshot.characterId != m_impl->runtimeSnapshot.characterId ||
			runtimeSnapshot.characterDataId != m_impl->runtimeSnapshot.characterDataId ||
			runtimeSnapshot.statRevision <= m_impl->runtimeSnapshot.statRevision)
		{
			return false;
		}

		m_impl->runtimeSnapshot = runtimeSnapshot;
		m_impl->currentHp = std::min(m_impl->currentHp, runtimeSnapshot.maxHp);
		m_impl->currentMp = std::min(m_impl->currentMp, runtimeSnapshot.maxMp);
		return true;
	}

	bool FPlayerEntity::ApplyMove(
		const SMoveResult& moveResult) noexcept
	{
		if (!IsAlive() || moveResult.entityId != GetEntityId() || moveResult.sequence <= m_impl->lastMoveSequence ||
			moveResult.currentSectorId == kInvalidSectorId || !IsFinite(moveResult.acceptedPosition) || !IsFinite(moveResult.direction))
		{
			return false;
		}

		if (!ApplySpatialState(moveResult.acceptedPosition, moveResult.direction, moveResult.currentSectorId))
		{
			return false;
		}
		m_impl->lastMoveSequence = moveResult.sequence;
		m_impl->moveState = moveResult.moveState;
		return true;
	}

	FPlayerEntity::SCommittedMoveState FPlayerEntity::CaptureCommittedMoveState() const noexcept
	{
		return {GetPosition(), GetDirection(), GetSectorId(), m_impl->lastMoveSequence, m_impl->moveState};
	}

	bool FPlayerEntity::RestoreCommittedMoveState(
		const SCommittedMoveState& state) noexcept
	{
		if (state.sectorId == kInvalidSectorId || !IsFinite(state.position) || !IsFinite(state.direction) ||
			!ApplySpatialState(state.position, state.direction, state.sectorId))
		{
			return false;
		}
		m_impl->lastMoveSequence = state.lastMoveSequence;
		m_impl->moveState = state.moveState;
		return true;
	}

	FPlayerEntity::SCommittedCombatState FPlayerEntity::CaptureCommittedCombatState() const noexcept
	{
		return {m_impl->currentHp, m_impl->currentMp, m_impl->nextBasicAttackTick};
	}

	bool FPlayerEntity::RestoreCommittedCombatState(
		const SCommittedCombatState& state) noexcept
	{
		if ((!m_impl->hasRuntimeSnapshot && (state.currentHp != 0 || state.currentMp != 0)) ||
			(m_impl->hasRuntimeSnapshot &&
				(state.currentHp > m_impl->runtimeSnapshot.maxHp || state.currentMp > m_impl->runtimeSnapshot.maxMp)))
		{
			return false;
		}

		m_impl->currentHp = state.currentHp;
		m_impl->currentMp = state.currentMp;
		m_impl->nextBasicAttackTick = state.nextBasicAttackTick;
		return true;
	}

	bool FPlayerEntity::CommitBasicAttackCooldown(
		const std::uint64_t currentTick,
		const std::uint64_t cooldownTicks) noexcept
	{
		if (!IsAlive() || cooldownTicks == 0 || currentTick < m_impl->nextBasicAttackTick ||
			currentTick > std::numeric_limits<std::uint64_t>::max() - cooldownTicks)
		{
			return false;
		}

		m_impl->nextBasicAttackTick = currentTick + cooldownTicks;
		return true;
	}

	FPlayerEntity::SCommittedLifecycleState FPlayerEntity::CaptureCommittedLifecycleState() const noexcept
	{
		return {CaptureCommittedMoveState(),
			CaptureCommittedCombatState(),
			m_impl->lifeState,
			m_impl->lifeRevision,
			m_impl->respawnDueTick,
			m_impl->lastKillerEntityId};
	}

	bool FPlayerEntity::RestoreCommittedLifecycleState(
		const SCommittedLifecycleState& state) noexcept
	{
		if ((state.lifeState != EPlayerLifeState::Alive && state.lifeState != EPlayerLifeState::Dead) || state.lifeRevision == 0 ||
			(state.lifeState == EPlayerLifeState::Alive &&
				(state.respawnDueTick != 0 || (m_impl->hasRuntimeSnapshot && state.combat.currentHp == 0))) ||
			(state.lifeState == EPlayerLifeState::Dead &&
				(!m_impl->hasRuntimeSnapshot || state.combat.currentHp != 0 || state.respawnDueTick == 0)) ||
			!RestoreCommittedMoveState(state.move) || !RestoreCommittedCombatState(state.combat))
		{
			return false;
		}

		m_impl->lifeState = state.lifeState;
		m_impl->lifeRevision = state.lifeRevision;
		m_impl->respawnDueTick = state.respawnDueTick;
		m_impl->lastKillerEntityId = state.lastKillerEntityId;
		return true;
	}

	bool FPlayerEntity::ApplyDamage(
		const std::uint32_t requestedDamage,
		std::uint32_t& outAppliedDamage) noexcept
	{
		outAppliedDamage = 0;
		if (!m_impl->hasRuntimeSnapshot || !IsAlive() || requestedDamage == 0 || m_impl->currentHp == 0)
		{
			return false;
		}

		outAppliedDamage = std::min(requestedDamage, m_impl->currentHp);
		m_impl->currentHp -= outAppliedDamage;
		return true;
	}

	bool FPlayerEntity::CommitDeath(
		const FEntityId killerEntityId,
		const std::uint64_t currentTick,
		const std::uint64_t respawnDelayTicks) noexcept
	{
		if (!m_impl->hasRuntimeSnapshot || !IsAlive() || m_impl->currentHp != 0 || killerEntityId == kInvalidEntityId ||
			respawnDelayTicks == 0 || currentTick > std::numeric_limits<std::uint64_t>::max() - respawnDelayTicks)
		{
			return false;
		}

		m_impl->lifeState = EPlayerLifeState::Dead;
		m_impl->moveState = EMoveState::Stop;
		m_impl->respawnDueTick = currentTick + respawnDelayTicks;
		m_impl->lastKillerEntityId = killerEntityId;
		return true;
	}

	bool FPlayerEntity::CommitRespawn(
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId) noexcept
	{
		if (!m_impl->hasRuntimeSnapshot || m_impl->lifeState != EPlayerLifeState::Dead || m_impl->currentHp != 0 ||
			m_impl->respawnDueTick == 0 || m_impl->lifeRevision == std::numeric_limits<std::uint64_t>::max() ||
			!ApplySpatialState(position, direction, sectorId))
		{
			return false;
		}

		m_impl->currentHp = m_impl->runtimeSnapshot.maxHp;
		m_impl->currentMp = m_impl->runtimeSnapshot.maxMp;
		m_impl->lifeState = EPlayerLifeState::Alive;
		m_impl->moveState = EMoveState::Stop;
		++m_impl->lifeRevision;
		m_impl->respawnDueTick = 0;
		m_impl->lastKillerEntityId = kInvalidEntityId;
		m_impl->nextBasicAttackTick = 0;
		return true;
	}
}
