#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FMonsterEntity.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorUpdateContext.h"
#include "WorldCore/Map/Sector/Systems/FPlayerAttackSystem.h"

namespace WorldCore
{
	bool FPlayerAttackSystem::Update(
		const SSectorUpdateContext& context,
		SSectorTaskOutput& outOutput,
		std::string& outError) const
	{
		if (context.mapDefinition.tickRateHz == 0 || context.mapDefinition.combatPolicy.playerBasicAttackRange <= 0.0f ||
			context.mapDefinition.combatPolicy.playerBasicAttackCooldownMilliseconds == 0)
		{
			outError = "Player Basic Attack requires a valid Combat policy.";
			return false;
		}

		const std::uint64_t cooldownTicks = std::max<std::uint64_t>(1,
			(static_cast<std::uint64_t>(context.mapDefinition.combatPolicy.playerBasicAttackCooldownMilliseconds) *
					context.mapDefinition.tickRateHz +
				999) /
				1'000);
		const float attackRange = context.mapDefinition.combatPolicy.playerBasicAttackRange;
		const float attackRangeSquared = attackRange * attackRange;
		std::unordered_set<FEntityId> acceptedAttackers;
		for (const SPlayerAttackCommand& command : context.task.playerAttackCommands)
		{
			const SPlayerAttackRequestIdentity identity{command.attackerEntityId, command.attackSequence};
			const FPlayerEntity* const attacker = context.entityRegistry.FindPlayer(command.attackerEntityId);
			if (attacker == nullptr || attacker->GetSectorId() != context.task.sectorId ||
				!context.sectorGrid.ContainsEntity(context.task.sectorId, command.attackerEntityId))
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::InvalidAttacker});
				continue;
			}
			if (!attacker->HasRuntimeSnapshot() || !attacker->IsAlive() || attacker->GetCurrentHp() == 0)
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::AttackerDead});
				continue;
			}

			const FMonsterEntity* const target = context.entityRegistry.FindMonster(command.targetEntityId);
			if (target == nullptr)
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::InvalidTarget});
				continue;
			}
			if (target->GetCurrentHp() == 0)
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::TargetDead});
				continue;
			}
			if (GetDistanceSquared(attacker->GetPosition(), target->GetPosition()) > attackRangeSquared)
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::OutOfRange});
				continue;
			}
			if (context.task.tickIndex < attacker->GetNextBasicAttackTick() || acceptedAttackers.contains(command.attackerEntityId) ||
				context.task.tickIndex > std::numeric_limits<std::uint64_t>::max() - cooldownTicks)
			{
				outOutput.rejectedPlayerAttacks.push_back({identity, command.targetEntityId, EPlayerAttackRejectReason::Cooldown});
				continue;
			}

			acceptedAttackers.insert(command.attackerEntityId);
			outOutput.playerAttackIntents.push_back({command.attackerEntityId,
				command.attackSequence,
				command.targetEntityId,
				context.task.sectorId,
				context.task.tickIndex,
				attacker->GetNextBasicAttackTick()});
		}
		return true;
	}
}
