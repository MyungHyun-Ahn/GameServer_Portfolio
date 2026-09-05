#pragma once

namespace WorldCore
{
	struct SVector2;

	class FActorEntity
	{
	public:
		virtual ~FActorEntity();

		FActorEntity(const FActorEntity&) = delete;
		FActorEntity& operator=(const FActorEntity&) = delete;
		FActorEntity(FActorEntity&& other) noexcept;
		FActorEntity& operator=(FActorEntity&& other) noexcept;

		[[nodiscard]] FEntityId GetEntityId() const noexcept;
		[[nodiscard]] EActorKind GetActorKind() const noexcept;
		[[nodiscard]] const SVector2& GetPosition() const noexcept;
		[[nodiscard]] const SVector2& GetDirection() const noexcept;
		[[nodiscard]] FSectorId GetSectorId() const noexcept;

	protected:
		FActorEntity(FEntityId entityId, EActorKind actorKind, const SVector2& position, const SVector2& direction, FSectorId sectorId);

		[[nodiscard]] bool ApplySpatialState(const SVector2& position, const SVector2& direction, FSectorId sectorId) noexcept;

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
