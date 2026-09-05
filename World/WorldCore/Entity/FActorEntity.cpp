#include "WorldCorePch.h"

#include "WorldCore/Entity/FActorEntity.h"

namespace WorldCore
{
	struct FActorEntity::SImpl final
	{
		FEntityId entityId = kInvalidEntityId;
		EActorKind actorKind = EActorKind::Player;
		SVector2 position{};
		SVector2 direction{};
		FSectorId sectorId = kInvalidSectorId;
	};

	FActorEntity::FActorEntity(
		const FEntityId entityId,
		const EActorKind actorKind,
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId)
		: m_impl(std::make_unique<SImpl>())
	{
		m_impl->entityId = entityId;
		m_impl->actorKind = actorKind;
		m_impl->position = position;
		m_impl->direction = NormalizeOrZero(direction);
		m_impl->sectorId = sectorId;
	}

	FActorEntity::~FActorEntity() = default;
	FActorEntity::FActorEntity(FActorEntity&& other) noexcept = default;
	FActorEntity& FActorEntity::operator=(FActorEntity&& other) noexcept = default;

	FEntityId FActorEntity::GetEntityId() const noexcept
	{
		return m_impl->entityId;
	}

	EActorKind FActorEntity::GetActorKind() const noexcept
	{
		return m_impl->actorKind;
	}

	const SVector2& FActorEntity::GetPosition() const noexcept
	{
		return m_impl->position;
	}

	const SVector2& FActorEntity::GetDirection() const noexcept
	{
		return m_impl->direction;
	}

	FSectorId FActorEntity::GetSectorId() const noexcept
	{
		return m_impl->sectorId;
	}

	bool FActorEntity::ApplySpatialState(
		const SVector2& position,
		const SVector2& direction,
		const FSectorId sectorId) noexcept
	{
		if (!IsFinite(position) || !IsFinite(direction) || sectorId == kInvalidSectorId)
		{
			return false;
		}

		m_impl->position = position;
		m_impl->direction = direction;
		m_impl->sectorId = sectorId;
		return true;
	}
}
