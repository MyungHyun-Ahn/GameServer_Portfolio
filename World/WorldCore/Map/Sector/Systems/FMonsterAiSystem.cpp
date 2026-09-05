#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorUpdateContext.h"
#include "WorldCore/Map/Sector/Systems/FMonsterAiSystem.h"

namespace WorldCore
{
	namespace
	{
		bool IsInsideRadius(
			const SVector2& lhs,
			const SVector2& rhs,
			const float radius) noexcept
		{
			return GetDistanceSquared(lhs, rhs) <= radius * radius;
		}

		bool IsAtSpawnPosition(
			const FMonsterEntity& monster) noexcept
		{
			return GetDistanceSquared(monster.GetPosition(), monster.GetSpawnPosition()) <= std::numeric_limits<float>::epsilon();
		}

		bool IsAvailableTarget(
			const FPlayerEntity& player) noexcept
		{
			return player.HasRuntimeSnapshot() && player.GetCurrentHp() > 0;
		}

		const FPlayerEntity* FindRetainedTarget(
			const FMonsterEntity& monster,
			const FEntityRegistry& entityRegistry)
		{
			const FPlayerEntity* const currentTarget = entityRegistry.FindPlayer(monster.GetTargetEntityId());
			if (currentTarget != nullptr && IsAvailableTarget(*currentTarget) &&
				IsInsideRadius(monster.GetSpawnPosition(), currentTarget->GetPosition(), monster.GetRuntimeSnapshot().leashRadius))
			{
				return currentTarget;
			}
			return nullptr;
		}

		const FPlayerEntity* FindAggressiveTarget(
			const FMonsterEntity& monster,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid)
		{
			if (monster.GetRuntimeSnapshot().aggroType != EMonsterAggroType::Aggressive)
			{
				return nullptr;
			}

			const FPlayerEntity* selectedTarget = nullptr;
			float selectedDistanceSquared = std::numeric_limits<float>::max();
			for (const FEntityId candidateId : sectorGrid.GetNearbyEntityIds(monster.GetSectorId(), 1))
			{
				const FPlayerEntity* const candidate = entityRegistry.FindPlayer(candidateId);
				if (candidate == nullptr || !IsAvailableTarget(*candidate))
				{
					continue;
				}

				const float distanceSquared = GetDistanceSquared(monster.GetPosition(), candidate->GetPosition());
				if (distanceSquared > monster.GetRuntimeSnapshot().aggroRadius * monster.GetRuntimeSnapshot().aggroRadius ||
					!IsInsideRadius(monster.GetSpawnPosition(), candidate->GetPosition(), monster.GetRuntimeSnapshot().leashRadius))
				{
					continue;
				}
				if (selectedTarget == nullptr || distanceSquared < selectedDistanceSquared ||
					(distanceSquared == selectedDistanceSquared && candidateId < selectedTarget->GetEntityId()))
				{
					selectedTarget = candidate;
					selectedDistanceSquared = distanceSquared;
				}
			}
			return selectedTarget;
		}

		bool AppendReturnResult(
			const FMonsterEntity& monster,
			const SSectorUpdateContext& context,
			SSectorTaskOutput& outOutput,
			std::string& outError)
		{
			SMonsterAiResult result{};
			result.entityId = monster.GetEntityId();
			result.previousSectorId = context.task.sectorId;
			result.currentSectorId = context.task.sectorId;
			result.acceptedPosition = monster.GetPosition();
			result.direction = monster.GetDirection();

			const SVector2 returnDelta{
				monster.GetSpawnPosition().x - monster.GetPosition().x, monster.GetSpawnPosition().y - monster.GetPosition().y};
			const float returnDistanceSquared = GetDistanceSquared(monster.GetPosition(), monster.GetSpawnPosition());
			if (IsAtSpawnPosition(monster))
			{
				result.aiState = EMonsterAiState::Idle;
				result.moveState = EMoveState::Stop;
				result.acceptedPosition = monster.GetSpawnPosition();
				outOutput.monsterAiResults.push_back(result);
				return true;
			}

			const float returnDistance = std::sqrt(returnDistanceSquared);
			const float maximumStep = monster.GetRuntimeSnapshot().moveSpeed / static_cast<float>(context.mapDefinition.tickRateHz);
			result.direction = NormalizeOrZero(returnDelta);
			if (maximumStep >= returnDistance)
			{
				result.aiState = EMonsterAiState::Idle;
				result.moveState = EMoveState::Stop;
				result.acceptedPosition = monster.GetSpawnPosition();
			}
			else
			{
				result.aiState = EMonsterAiState::Return;
				result.moveState = EMoveState::Start;
				result.acceptedPosition = context.sectorGrid.ClampInsideWorld({monster.GetPosition().x + result.direction.x * maximumStep,
					monster.GetPosition().y + result.direction.y * maximumStep});
			}

			if (!context.sectorGrid.TryResolveSector(result.acceptedPosition, result.currentSectorId))
			{
				outError = "Monster Return position cannot be resolved to a Sector.";
				return false;
			}
			if (result.previousSectorId != result.currentSectorId)
			{
				outOutput.sectorTransfers.push_back({result.entityId, result.previousSectorId, result.currentSectorId});
			}
			outOutput.monsterAiResults.push_back(result);
			return true;
		}
	}

	bool FMonsterAiSystem::Update(
		const SSectorUpdateContext& context,
		SSectorTaskOutput& outOutput,
		std::string& outError) const
	{
		if (context.mapDefinition.tickRateHz == 0)
		{
			outError = "Monster AI requires a non-zero Map Tick rate.";
			return false;
		}

		const std::vector<FEntityId> entityIds = context.sectorGrid.GetEntityIdsInSector(context.task.sectorId);
		outOutput.monsterAiResults.reserve(entityIds.size());
		outOutput.monsterAttackIntents.reserve(entityIds.size());
		for (const FEntityId entityId : entityIds)
		{
			const FMonsterEntity* const monster = context.entityRegistry.FindMonster(entityId);
			if (monster == nullptr)
			{
				continue;
			}
			if (monster->GetSectorId() != context.task.sectorId || !context.sectorGrid.ContainsEntity(context.task.sectorId, entityId))
			{
				outError = "Monster ownership does not match the Sector task.";
				return false;
			}

			if (monster->GetAiState() == EMonsterAiState::Return ||
				!IsInsideRadius(monster->GetSpawnPosition(), monster->GetPosition(), monster->GetRuntimeSnapshot().leashRadius))
			{
				if (!AppendReturnResult(*monster, context, outOutput, outError))
				{
					return false;
				}
				continue;
			}

			SMonsterAiResult result{};
			result.entityId = entityId;
			result.previousSectorId = context.task.sectorId;
			result.currentSectorId = context.task.sectorId;
			result.acceptedPosition = monster->GetPosition();
			result.direction = monster->GetDirection();

			const bool hadTarget = monster->GetTargetEntityId() != kInvalidEntityId;
			const FPlayerEntity* target = FindRetainedTarget(*monster, context.entityRegistry);
			if (hadTarget && target == nullptr)
			{
				if (!AppendReturnResult(*monster, context, outOutput, outError))
				{
					return false;
				}
				continue;
			}
			if (target == nullptr)
			{
				target = FindAggressiveTarget(*monster, context.entityRegistry, context.sectorGrid);
			}
			if (target == nullptr)
			{
				if (!IsAtSpawnPosition(*monster))
				{
					if (!AppendReturnResult(*monster, context, outOutput, outError))
					{
						return false;
					}
					continue;
				}
				result.aiState = EMonsterAiState::Idle;
				result.moveState = EMoveState::Stop;
				outOutput.monsterAiResults.push_back(result);
				continue;
			}

			result.targetEntityId = target->GetEntityId();
			const SVector2 targetDelta{
				target->GetPosition().x - monster->GetPosition().x, target->GetPosition().y - monster->GetPosition().y};
			const float distanceSquared = GetDistanceSquared(monster->GetPosition(), target->GetPosition());
			const float attackRange = monster->GetRuntimeSnapshot().attackRange;
			const float attackRangeSquared = attackRange * attackRange;
			const SVector2 direction = NormalizeOrZero(targetDelta);
			if (direction != SVector2{})
			{
				result.direction = direction;
			}

			if (distanceSquared <= attackRangeSquared)
			{
				result.aiState = EMonsterAiState::AttackReady;
				result.moveState = EMoveState::Stop;
				if (context.task.tickIndex >= monster->GetNextAttackTick())
				{
					outOutput.monsterAttackIntents.push_back({monster->GetEntityId(),
						target->GetEntityId(),
						monster->GetSpawnGeneration(),
						monster->GetSectorId(),
						context.task.tickIndex,
						monster->GetNextAttackTick()});
				}
				outOutput.monsterAiResults.push_back(result);
				continue;
			}

			const float distance = std::sqrt(distanceSquared);
			const float maximumStep = monster->GetRuntimeSnapshot().moveSpeed / static_cast<float>(context.mapDefinition.tickRateHz);
			const float step = std::min(maximumStep, distance - attackRange);
			result.aiState = EMonsterAiState::Chase;
			result.moveState = EMoveState::Start;
			result.acceptedPosition = context.sectorGrid.ClampInsideWorld(
				{monster->GetPosition().x + result.direction.x * step, monster->GetPosition().y + result.direction.y * step});
			if (!IsInsideRadius(monster->GetSpawnPosition(), result.acceptedPosition, monster->GetRuntimeSnapshot().leashRadius))
			{
				if (!AppendReturnResult(*monster, context, outOutput, outError))
				{
					return false;
				}
				continue;
			}
			if (!context.sectorGrid.TryResolveSector(result.acceptedPosition, result.currentSectorId))
			{
				outError = "Monster AI position cannot be resolved to a Sector.";
				return false;
			}
			if (result.previousSectorId != result.currentSectorId)
			{
				outOutput.sectorTransfers.push_back({entityId, result.previousSectorId, result.currentSectorId});
			}
			outOutput.monsterAiResults.push_back(result);
		}
		return true;
	}
}
