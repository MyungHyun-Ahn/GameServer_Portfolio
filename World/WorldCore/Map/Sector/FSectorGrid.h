#pragma once

namespace WorldCore
{
	struct SVector2;

	class FSectorGrid final
	{
	public:
		explicit FSectorGrid(const SMapDefinition& definition);
		~FSectorGrid();

		FSectorGrid(const FSectorGrid&) = delete;
		FSectorGrid& operator=(const FSectorGrid&) = delete;

		[[nodiscard]] std::uint32_t GetColumnCount() const noexcept;
		[[nodiscard]] std::uint32_t GetRowCount() const noexcept;
		[[nodiscard]] std::uint32_t GetSectorCount() const noexcept;
		[[nodiscard]] bool IsValidSectorId(FSectorId sectorId) const noexcept;
		[[nodiscard]] bool TryResolveSector(const SVector2& position, FSectorId& outSectorId) const noexcept;
		[[nodiscard]] SVector2 ClampInsideWorld(const SVector2& position) const noexcept;
		[[nodiscard]] std::vector<FSectorId> GetNearbySectorIds(FSectorId centerSectorId, std::uint32_t radius) const;
		[[nodiscard]] std::vector<FEntityId> GetEntityIdsInSector(FSectorId sectorId) const;
		[[nodiscard]] std::vector<FEntityId> GetNearbyEntityIds(FSectorId centerSectorId, std::uint32_t radius) const;
		[[nodiscard]] std::optional<FSectorId> GetEntitySectorId(FEntityId entityId) const noexcept;
		[[nodiscard]] bool ContainsEntity(FSectorId sectorId, FEntityId entityId) const noexcept;
		[[nodiscard]] bool AddEntity(FSectorId sectorId, FEntityId entityId);
		[[nodiscard]] bool RemoveEntity(FEntityId entityId);
		[[nodiscard]] bool TransferEntity(FEntityId entityId, FSectorId sourceSectorId, FSectorId targetSectorId);

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_impl;
	};
}
