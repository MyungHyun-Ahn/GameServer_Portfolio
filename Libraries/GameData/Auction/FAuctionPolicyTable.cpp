#include "GameDataPch.h"

#include "GameData/Auction/FAuctionPolicyTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Auction
{
	namespace
	{
		constexpr std::string_view kSectionName = "Auction";
		constexpr std::array<std::string_view, 1> kKnownSections = {kSectionName};
		constexpr std::array<std::string_view, 5> kKnownKeys = {"MaxActiveListings",
			"SearchPageSize",
			"MinimumListingDurationSeconds",
			"MaximumListingDurationSeconds",
			"DefaultListingDurationSeconds"};
	}

	bool FAuctionPolicyTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);
		SAuctionPolicy loaded;
		if (!reader.ValidateKnownSections(kKnownSections, outError) || !reader.ValidateKnownKeys(kSectionName, kKnownKeys, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MaxActiveListings", loaded.maxActiveListings, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "SearchPageSize", loaded.searchPageSize, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MinimumListingDurationSeconds", loaded.minimumListingDurationSeconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MaximumListingDurationSeconds", loaded.maximumListingDurationSeconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "DefaultListingDurationSeconds", loaded.defaultListingDurationSeconds, outError))
		{
			return false;
		}

		if (loaded.maxActiveListings == 0 || loaded.maxActiveListings >= 100 || loaded.searchPageSize == 0 ||
			loaded.searchPageSize >= 100 || loaded.minimumListingDurationSeconds == 0 ||
			loaded.minimumListingDurationSeconds > loaded.maximumListingDurationSeconds ||
			loaded.defaultListingDurationSeconds < loaded.minimumListingDurationSeconds ||
			loaded.defaultListingDurationSeconds > loaded.maximumListingDurationSeconds)
		{
			outError = "invalid auction policy range.";
			return false;
		}

		m_policy = loaded;
		outError.clear();
		return true;
	}

	const SAuctionPolicy& FAuctionPolicyTable::Get() const noexcept
	{
		return m_policy;
	}
}
