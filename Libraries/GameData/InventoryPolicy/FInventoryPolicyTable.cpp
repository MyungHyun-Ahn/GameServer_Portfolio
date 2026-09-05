#include "GameDataPch.h"

#include "GameData/InventoryPolicy/FInventoryPolicyTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::InventoryPolicy
{
	namespace
	{
		constexpr std::string_view kSectionName = "InventoryPolicy1";
		constexpr std::array<std::string_view, 1> kKnownSections = {kSectionName};
		constexpr std::array<std::string_view, 3> kKnownKeys = {"InventoryPolicyId", "MaxInventorySlots", "InventoryListPageSize"};
	}

	bool FInventoryPolicyTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);
		SInventoryPolicyData loaded;
		if (!reader.ValidateKnownSections(kKnownSections, outError) || !reader.ValidateKnownKeys(kSectionName, kKnownKeys, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "InventoryPolicyId", loaded.inventoryPolicyId, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MaxInventorySlots", loaded.maxInventorySlots, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "InventoryListPageSize", loaded.inventoryListPageSize, outError))
		{
			return false;
		}

		if (loaded.inventoryPolicyId != 1 || loaded.maxInventorySlots == 0 || loaded.maxInventorySlots > 10000 ||
			loaded.inventoryListPageSize == 0 || loaded.inventoryListPageSize >= 100 ||
			loaded.inventoryListPageSize > loaded.maxInventorySlots)
		{
			outError = "invalid inventory policy range.";
			return false;
		}

		m_policy = loaded;
		outError.clear();
		return true;
	}

	const SInventoryPolicyData& FInventoryPolicyTable::Get() const noexcept
	{
		return m_policy;
	}
}
