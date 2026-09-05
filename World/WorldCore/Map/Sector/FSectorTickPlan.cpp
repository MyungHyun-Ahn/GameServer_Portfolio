#include "WorldCorePch.h"

#include "WorldCore/Map/Sector/FSectorGrid.h"
#include "WorldCore/Map/Sector/FSectorTickPlan.h"

namespace WorldCore
{
	namespace
	{
		constexpr std::uint32_t kUnassignedWaveIndex = std::numeric_limits<std::uint32_t>::max();

		std::uint32_t ResolveWaveIndex(
			const FSectorId sectorId,
			const std::uint32_t columnCount) noexcept
		{
			const std::uint32_t x = sectorId % columnCount;
			const std::uint32_t y = sectorId / columnCount;
			return (x & 1u) | ((y & 1u) << 1u);
		}

		bool ValidateNoAdjacentSectorsInSameWave(
			const std::uint32_t columnCount,
			const std::uint32_t rowCount,
			const std::span<const std::uint32_t> waveBySector,
			std::string& outError)
		{
			for (std::uint32_t y = 0; y < rowCount; ++y)
			{
				for (std::uint32_t x = 0; x < columnCount; ++x)
				{
					const FSectorId sectorId = y * columnCount + x;
					for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
					{
						for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
						{
							if (offsetX == 0 && offsetY == 0)
							{
								continue;
							}

							const std::int32_t neighborX = static_cast<std::int32_t>(x) + offsetX;
							const std::int32_t neighborY = static_cast<std::int32_t>(y) + offsetY;
							if (neighborX < 0 || neighborY < 0 || neighborX >= static_cast<std::int32_t>(columnCount) ||
								neighborY >= static_cast<std::int32_t>(rowCount))
							{
								continue;
							}

							const FSectorId neighborId =
								static_cast<FSectorId>(neighborY) * columnCount + static_cast<FSectorId>(neighborX);
							if (neighborId > sectorId && waveBySector[neighborId] == waveBySector[sectorId])
							{
								outError = "Sector Tick Plan placed adjacent Sectors in the same Wave.";
								return false;
							}
						}
					}
				}
			}
			return true;
		}
	}

	struct FSectorTickPlan::SImpl final
	{
		std::uint64_t tickIndex = 0;
		std::size_t taskCount = 0;
		std::vector<SSectorTaskWave> waves;
	};

	FSectorTickPlan::FSectorTickPlan()
		: m_impl(std::make_unique<SImpl>())
	{
	}

	FSectorTickPlan::~FSectorTickPlan() = default;

	bool FSectorTickPlan::Build(
		const FSectorGrid& sectorGrid,
		std::vector<SSectorTask> tasks,
		std::string& outError)
	{
		Clear();
		outError.clear();

		const std::uint32_t columnCount = sectorGrid.GetColumnCount();
		const std::uint32_t rowCount = sectorGrid.GetRowCount();
		const std::uint32_t sectorCount = sectorGrid.GetSectorCount();
		if (columnCount == 0 || rowCount == 0 || sectorCount == 0)
		{
			outError = "Sector Tick Plan requires a non-empty Sector Grid.";
			return false;
		}
		if (tasks.size() != sectorCount)
		{
			outError = "Sector Tick Plan must contain exactly one Task for every Sector.";
			return false;
		}

		const std::uint64_t tickIndex = tasks.front().tickIndex;
		if (tickIndex == 0)
		{
			outError = "Sector Tick Plan contains an invalid Tick index.";
			return false;
		}

		std::vector<std::uint8_t> seenSectors(sectorCount, 0);
		std::vector<std::uint32_t> waveBySector(sectorCount, kUnassignedWaveIndex);
		std::vector<SSectorTaskWave> waves(kSectorTaskWaveCount);
		for (std::uint32_t waveIndex = 0; waveIndex < kSectorTaskWaveCount; ++waveIndex)
		{
			waves[waveIndex].waveIndex = waveIndex;
		}

		for (SSectorTask& task : tasks)
		{
			if (!sectorGrid.IsValidSectorId(task.sectorId))
			{
				outError = "Sector Tick Plan contains an invalid SectorId.";
				return false;
			}
			if (task.stableOrder != task.sectorId)
			{
				outError = "Sector Tick Plan stable order must match row-major SectorId order.";
				return false;
			}
			if (task.tickIndex != tickIndex)
			{
				outError = "Sector Tick Plan contains Tasks from different Ticks.";
				return false;
			}
			if (seenSectors[task.sectorId] != 0)
			{
				outError = "Sector Tick Plan contains a duplicate Sector Task.";
				return false;
			}

			seenSectors[task.sectorId] = 1;
			const std::uint32_t waveIndex = ResolveWaveIndex(task.sectorId, columnCount);
			waveBySector[task.sectorId] = waveIndex;
			waves[waveIndex].tasks.push_back(std::move(task));
		}

		if (std::any_of(seenSectors.begin(),
				seenSectors.end(),
				[](const std::uint8_t seen)
				{
					return seen == 0;
				}))
		{
			outError = "Sector Tick Plan is missing a Sector Task.";
			return false;
		}

		for (SSectorTaskWave& wave : waves)
		{
			std::sort(wave.tasks.begin(),
				wave.tasks.end(),
				[](const SSectorTask& lhs, const SSectorTask& rhs)
				{
					return lhs.stableOrder < rhs.stableOrder;
				});
		}

		if (!ValidateNoAdjacentSectorsInSameWave(columnCount, rowCount, waveBySector, outError))
		{
			return false;
		}

		m_impl->tickIndex = tickIndex;
		m_impl->taskCount = sectorCount;
		m_impl->waves = std::move(waves);
		return true;
	}

	void FSectorTickPlan::Clear() noexcept
	{
		m_impl->tickIndex = 0;
		m_impl->taskCount = 0;
		m_impl->waves.clear();
	}

	std::uint64_t FSectorTickPlan::GetTickIndex() const noexcept
	{
		return m_impl->tickIndex;
	}

	std::size_t FSectorTickPlan::GetTaskCount() const noexcept
	{
		return m_impl->taskCount;
	}

	std::span<const SSectorTaskWave> FSectorTickPlan::GetWaves() const noexcept
	{
		return m_impl->waves;
	}
}
