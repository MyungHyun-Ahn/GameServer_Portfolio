#include "WorldCorePch.h"

#include "WorldCore/Entity/FEntityRegistry.h"
#include "WorldCore/Entity/FPlayerEntity.h"
#include "WorldCore/Map/FMapVisibilitySystem.h"
#include "WorldCore/Map/Sector/FSectorGrid.h"

namespace WorldCore
{
	struct FMapVisibilitySystem::SImpl final
	{
		std::unordered_map<FEntityId, std::set<FEntityId>> visibleSubjectsByObserver;
	};

	FMapVisibilitySystem::FMapVisibilitySystem()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FMapVisibilitySystem::~FMapVisibilitySystem() = default;

	std::vector<SVisibilityEvent> FMapVisibilitySystem::Refresh(
		const FEntityRegistry& entityRegistry,
		const FSectorGrid& sectorGrid,
		const std::uint32_t visibilitySectorRadius,
		const std::span<const FEntityId> movedEntityIds)
	{
		std::vector<SVisibilityEvent> events;
		const std::unordered_set<FEntityId> movedSet(movedEntityIds.begin(), movedEntityIds.end());
		const std::vector<FEntityId> observerIds = entityRegistry.GetPlayerEntityIds();
		std::unordered_map<FEntityId, std::set<FEntityId>> nextVisibility;
		nextVisibility.reserve(observerIds.size());

		for (const FEntityId observerId : observerIds)
		{
			const FPlayerEntity* const observer = entityRegistry.FindPlayer(observerId);
			if (observer == nullptr)
			{
				continue;
			}

			std::set<FEntityId> currentSubjects;
			for (const FEntityId subjectId : sectorGrid.GetNearbyEntityIds(observer->GetSectorId(), visibilitySectorRadius))
			{
				if (subjectId != observerId && entityRegistry.Contains(subjectId))
				{
					currentSubjects.insert(subjectId);
				}
			}

			const auto previousFound = m_impl->visibleSubjectsByObserver.find(observerId);
			const std::set<FEntityId> emptyPrevious;
			const std::set<FEntityId>& previousSubjects =
				previousFound == m_impl->visibleSubjectsByObserver.end() ? emptyPrevious : previousFound->second;

			for (const FEntityId subjectId : currentSubjects)
			{
				const FActorEntity* const subject = entityRegistry.FindActor(subjectId);
				if (subject == nullptr)
				{
					continue;
				}
				const FPlayerEntity* const subjectPlayer = entityRegistry.FindPlayer(subjectId);
				const FMoveSequence moveSequence = subjectPlayer == nullptr ? 0 : subjectPlayer->GetLastMoveSequence();

				if (!previousSubjects.contains(subjectId))
				{
					events.push_back({EVisibilityEventKind::Spawn,
						observerId,
						subjectId,
						subject->GetPosition(),
						subject->GetDirection(),
						moveSequence});
				}
				else if (movedSet.contains(subjectId))
				{
					events.push_back(
						{EVisibilityEventKind::Move, observerId, subjectId, subject->GetPosition(), subject->GetDirection(), moveSequence});
				}
			}

			for (const FEntityId subjectId : previousSubjects)
			{
				if (!currentSubjects.contains(subjectId))
				{
					events.push_back({EVisibilityEventKind::Despawn, observerId, subjectId});
				}
			}
			nextVisibility.emplace(observerId, std::move(currentSubjects));
		}

		m_impl->visibleSubjectsByObserver = std::move(nextVisibility);
		return events;
	}

	std::vector<SActorAttackEvent> FMapVisibilitySystem::BuildActorAttackEvents(
		const FEntityRegistry& entityRegistry,
		const std::span<const SActorAttackResult> attackResults) const
	{
		std::vector<SActorAttackEvent> events;
		const std::vector<FEntityId> observerIds = entityRegistry.GetPlayerEntityIds();
		for (const SActorAttackResult& attack : attackResults)
		{
			for (const FEntityId observerId : observerIds)
			{
				const auto visibleSubjects = m_impl->visibleSubjectsByObserver.find(observerId);
				const bool isTarget = observerId == attack.targetEntityId;
				const bool seesTarget =
					visibleSubjects != m_impl->visibleSubjectsByObserver.end() && visibleSubjects->second.contains(attack.targetEntityId);
				if (isTarget || seesTarget)
				{
					events.push_back({observerId, attack});
				}
			}
		}
		return events;
	}

	std::vector<SActorDeathEvent> FMapVisibilitySystem::BuildActorDeathEvents(
		const FEntityRegistry& entityRegistry,
		const std::span<const SActorDeathResult> deathResults) const
	{
		std::vector<SActorDeathEvent> events;
		const std::vector<FEntityId> observerIds = entityRegistry.GetPlayerEntityIds();
		for (const SActorDeathResult& death : deathResults)
		{
			for (const FEntityId observerId : observerIds)
			{
				const auto visibleSubjects = m_impl->visibleSubjectsByObserver.find(observerId);
				const bool isSubject = observerId == death.entityId;
				const bool seesSubject =
					visibleSubjects != m_impl->visibleSubjectsByObserver.end() && visibleSubjects->second.contains(death.entityId);
				if (isSubject || seesSubject)
				{
					events.push_back({observerId, death});
				}
			}
		}
		return events;
	}

	std::vector<SActorRespawnEvent> FMapVisibilitySystem::BuildActorRespawnEvents(
		const FEntityRegistry& entityRegistry,
		const std::span<const SActorRespawnResult> respawnResults) const
	{
		std::vector<SActorRespawnEvent> events;
		const std::vector<FEntityId> observerIds = entityRegistry.GetPlayerEntityIds();
		for (const SActorRespawnResult& respawn : respawnResults)
		{
			for (const FEntityId observerId : observerIds)
			{
				const auto visibleSubjects = m_impl->visibleSubjectsByObserver.find(observerId);
				const bool isSubject = observerId == respawn.entityId;
				const bool seesSubject =
					visibleSubjects != m_impl->visibleSubjectsByObserver.end() && visibleSubjects->second.contains(respawn.entityId);
				if (isSubject || seesSubject)
				{
					events.push_back({observerId, respawn});
				}
			}
		}
		return events;
	}

	void FMapVisibilitySystem::Reset()
	{
		m_impl->visibleSubjectsByObserver.clear();
	}
}
