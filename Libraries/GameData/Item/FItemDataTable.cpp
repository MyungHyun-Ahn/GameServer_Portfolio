#include "GameDataPch.h"

#include "GameData/Item/FItemDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Item
{
	namespace
	{
		constexpr std::array<std::string_view, 9> kKnownKeys =
			{"ItemDataId", "Name", "Category", "MaxStack", "Tradable", "Str", "Dex", "Int", "Luk"};

		constexpr std::array<Foundation::Config::SConfigEnumValue<EItemCategory>, 3> kCategoryValues = {
			{{"Equipment", EItemCategory::Equipment}, {"Consumable", EItemCategory::Consumable}, {"Material", EItemCategory::Material}}};
	}

	bool FItemDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SItemTemplate> loaded;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& entry : document.sections)
		{
			const std::string& sectionName = entry.first;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SItemTemplate item;
			if (!reader.ReadRequiredUInt32(sectionName, "ItemDataId", item.itemDataId, outError) ||
				!reader.ReadRequiredString(sectionName, "Name", item.name, outError) ||
				!reader.ReadRequiredEnum(sectionName, "Category", kCategoryValues, item.category, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MaxStack", item.maxStack, outError) ||
				!reader.ReadRequiredBool(sectionName, "Tradable", item.tradable, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Str", item.equipmentStats.str, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Dex", item.equipmentStats.dex, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Int", item.equipmentStats.intelligence, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "Luk", item.equipmentStats.luk, outError))
			{
				return false;
			}

			if (item.itemDataId == 0 || item.name.empty() || item.maxStack == 0)
			{
				outError = "invalid item data in section: " + sectionName;
				return false;
			}
			if (item.category == EItemCategory::Equipment && item.maxStack != 1)
			{
				outError = "equipment MaxStack must be 1: " + sectionName;
				return false;
			}
			if (item.category != EItemCategory::Equipment && !item.equipmentStats.IsZero())
			{
				outError = "non-equipment item stats must be zero: " + sectionName;
				return false;
			}
			const std::uint32_t itemDataId = item.itemDataId;
			if (!loaded.emplace(itemDataId, std::move(item)).second)
			{
				outError = "duplicate ItemDataId in section: " + sectionName;
				return false;
			}
		}

		if (loaded.empty())
		{
			outError = "item data table is empty.";
			return false;
		}
		m_templates = std::move(loaded);
		outError.clear();
		return true;
	}

	const SItemTemplate* FItemDataTable::Find(
		const std::uint32_t itemDataId) const noexcept
	{
		const auto it = m_templates.find(itemDataId);
		return it == m_templates.end() ? nullptr : &it->second;
	}

	std::size_t FItemDataTable::Size() const noexcept
	{
		return m_templates.size();
	}
}
