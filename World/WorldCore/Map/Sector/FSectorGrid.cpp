#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSectorGrid.h"

namespace WorldCore
{
	struct FSectorGrid::SImpl final
	{
		std::uint32_t worldWidth = 0;
		std::uint32_t worldHeight = 0;
		std::uint32_t sectorSize = 0;
		std::uint32_t columnCount = 0;
		std::uint32_t rowCount = 0;
		std::vector<std::set<FEntityId>> sectorEntityIds;
		std::unordered_map<FEntityId, FSectorId> entitySectors;
	};

	FSectorGrid::FSectorGrid(
		const SMapDefinition& definition)
		: m_impl(std::make_unique<SImpl>())
	{
		m_impl->worldWidth = definition.worldWidth;
		m_impl->worldHeight = definition.worldHeight;
		m_impl->sectorSize = definition.sectorSize;
		m_impl->columnCount = definition.sectorSize == 0 ? 0 : definition.worldWidth / definition.sectorSize;
		m_impl->rowCount = definition.sectorSize == 0 ? 0 : definition.worldHeight / definition.sectorSize;
		m_impl->sectorEntityIds.resize(static_cast<std::size_t>(m_impl->columnCount) * m_impl->rowCount);
	}

	FSectorGrid::~FSectorGrid() = default;

	std::uint32_t FSectorGrid::GetColumnCount() const noexcept
	{
		return m_impl->columnCount;
	}

	std::uint32_t FSectorGrid::GetRowCount() const noexcept
	{
		return m_impl->rowCount;
	}

	std::uint32_t FSectorGrid::GetSectorCount() const noexcept
	{
		return static_cast<std::uint32_t>(m_impl->sectorEntityIds.size());
	}

	bool FSectorGrid::IsValidSectorId(
		const FSectorId sectorId) const noexcept
	{
		return static_cast<std::size_t>(sectorId) < m_impl->sectorEntityIds.size();
	}

	bool FSectorGrid::TryResolveSector(
		const SVector2& position,
		FSectorId& outSectorId) const noexcept
	{
		outSectorId = kInvalidSectorId;
		if (!IsFinite(position) || position.x < 0.0f || position.y < 0.0f || position.x >= static_cast<float>(m_impl->worldWidth) ||
			position.y >= static_cast<float>(m_impl->worldHeight) || m_impl->sectorSize == 0)
		{
			return false;
		}

		const auto x = static_cast<std::uint32_t>(position.x) / m_impl->sectorSize;
		const auto y = static_cast<std::uint32_t>(position.y) / m_impl->sectorSize;
		outSectorId = y * m_impl->columnCount + x;
		return IsValidSectorId(outSectorId);
	}

	SVector2 FSectorGrid::ClampInsideWorld(
		const SVector2& position) const noexcept
	{
		if (!IsFinite(position) || m_impl->worldWidth == 0 || m_impl->worldHeight == 0)
		{
			return {};
		}

		const float maximumX = std::nextafter(static_cast<float>(m_impl->worldWidth), 0.0f);
		const float maximumY = std::nextafter(static_cast<float>(m_impl->worldHeight), 0.0f);
		return {std::clamp(position.x, 0.0f, maximumX), std::clamp(position.y, 0.0f, maximumY)};
	}

	std::vector<FSectorId> FSectorGrid::GetNearbySectorIds(
		const FSectorId centerSectorId,
		const std::uint32_t radius) const
	{
		std::vector<FSectorId> sectorIds;
		if (!IsValidSectorId(centerSectorId) || m_impl->columnCount == 0)
		{
			return sectorIds;
		}

		const std::uint32_t centerX = centerSectorId % m_impl->columnCount;
		const std::uint32_t centerY = centerSectorId / m_impl->columnCount;
		const std::uint32_t minimumX = centerX > radius ? centerX - radius : 0;
		const std::uint32_t minimumY = centerY > radius ? centerY - radius : 0;
		const std::uint32_t maximumX = centerX + std::min(radius, m_impl->columnCount - 1 - centerX);
		const std::uint32_t maximumY = centerY + std::min(radius, m_impl->rowCount - 1 - centerY);

		sectorIds.reserve(static_cast<std::size_t>(maximumX - minimumX + 1) * (maximumY - minimumY + 1));
		for (std::uint32_t y = minimumY; y <= maximumY; ++y)
		{
			for (std::uint32_t x = minimumX; x <= maximumX; ++x)
			{
				sectorIds.push_back(y * m_impl->columnCount + x);
			}
		}
		return sectorIds;
	}

	std::vector<FEntityId> FSectorGrid::GetEntityIdsInSector(
		const FSectorId sectorId) const
	{
		if (!IsValidSectorId(sectorId))
		{
			return {};
		}
		const auto& entityIds = m_impl->sectorEntityIds[sectorId];
		return {entityIds.begin(), entityIds.end()};
	}

	std::vector<FEntityId> FSectorGrid::GetNearbyEntityIds(
		const FSectorId centerSectorId,
		const std::uint32_t radius) const
	{
		std::set<FEntityId> uniqueEntityIds;
		for (const FSectorId sectorId : GetNearbySectorIds(centerSectorId, radius))
		{
			const auto& sectorEntities = m_impl->sectorEntityIds[sectorId];
			uniqueEntityIds.insert(sectorEntities.begin(), sectorEntities.end());
		}
		return {uniqueEntityIds.begin(), uniqueEntityIds.end()};
	}

	std::optional<FSectorId> FSectorGrid::GetEntitySectorId(
		const FEntityId entityId) const noexcept
	{
		const auto found = m_impl->entitySectors.find(entityId);
		return found == m_impl->entitySectors.end() ? std::nullopt : std::optional<FSectorId>(found->second);
	}

	bool FSectorGrid::ContainsEntity(
		const FSectorId sectorId,
		const FEntityId entityId) const noexcept
	{
		return IsValidSectorId(sectorId) && m_impl->sectorEntityIds[sectorId].contains(entityId);
	}

	bool FSectorGrid::AddEntity(
		const FSectorId sectorId,
		const FEntityId entityId)
	{
		if (!IsValidSectorId(sectorId) || entityId == kInvalidEntityId || m_impl->entitySectors.contains(entityId))
		{
			return false;
		}

		if (!m_impl->sectorEntityIds[sectorId].insert(entityId).second)
		{
			return false;
		}
		m_impl->entitySectors.emplace(entityId, sectorId);
		return true;
	}

	bool FSectorGrid::RemoveEntity(
		const FEntityId entityId)
	{
		const auto found = m_impl->entitySectors.find(entityId);
		if (found == m_impl->entitySectors.end())
		{
			return false;
		}
		m_impl->sectorEntityIds[found->second].erase(entityId);
		m_impl->entitySectors.erase(found);
		return true;
	}

	bool FSectorGrid::TransferEntity(
		const FEntityId entityId,
		const FSectorId sourceSectorId,
		const FSectorId targetSectorId)
	{
		if (!IsValidSectorId(sourceSectorId) || !IsValidSectorId(targetSectorId) || sourceSectorId == targetSectorId)
		{
			return false;
		}

		const auto found = m_impl->entitySectors.find(entityId);
		if (found == m_impl->entitySectors.end() || found->second != sourceSectorId ||
			!m_impl->sectorEntityIds[sourceSectorId].contains(entityId) || m_impl->sectorEntityIds[targetSectorId].contains(entityId))
		{
			return false;
		}

		m_impl->sectorEntityIds[sourceSectorId].erase(entityId);
		m_impl->sectorEntityIds[targetSectorId].insert(entityId);
		found->second = targetSectorId;
		return true;
	}
}
