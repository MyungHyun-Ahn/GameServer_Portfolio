#pragma once

namespace GameData::Item
{
	enum class EItemCategory : std::uint8_t
	{
		Equipment = 1,
		Consumable = 2,
		Material = 3
	};

	struct SEquipmentStats
	{
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;

		bool IsZero() const noexcept
		{
			return str == 0 && dex == 0 && intelligence == 0 && luk == 0;
		}
	};

	struct SItemTemplate
	{
		std::uint32_t itemDataId = 0;
		std::string name;
		EItemCategory category = EItemCategory::Material;
		std::uint32_t maxStack = 1;
		bool tradable = false;
		SEquipmentStats equipmentStats;
	};
}
