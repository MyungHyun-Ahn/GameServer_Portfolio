#include "GameDataPch.h"

#include "GameData/Auction/FAuctionPolicyTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Auction
{
	namespace
	{
		constexpr std::string_view kSectionName = "AuctionPolicy1";
		constexpr std::array<std::string_view, 1> kKnownSections = {kSectionName};
		constexpr std::array<std::string_view, 10> kKnownKeys = {"AuctionPolicyId",
			"MaxActiveListings",
			"SearchPageSize",
			"MinimumListingDurationSeconds",
			"MaximumListingDurationSeconds",
			"DefaultListingDurationSeconds",
			"DefaultCurrencyDataId",
			"MinimumBidIncrement",
			"MinimumListingPrice",
			"MaximumListingPrice"};
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
			!reader.ReadRequiredUInt32(kSectionName, "AuctionPolicyId", loaded.auctionPolicyId, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MaxActiveListings", loaded.maxActiveListings, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "SearchPageSize", loaded.searchPageSize, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MinimumListingDurationSeconds", loaded.minimumListingDurationSeconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MaximumListingDurationSeconds", loaded.maximumListingDurationSeconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "DefaultListingDurationSeconds", loaded.defaultListingDurationSeconds, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "DefaultCurrencyDataId", loaded.defaultCurrencyDataId, outError) ||
			!reader.ReadRequiredUInt64(kSectionName, "MinimumBidIncrement", loaded.minimumBidIncrement, outError) ||
			!reader.ReadRequiredUInt64(kSectionName, "MinimumListingPrice", loaded.minimumListingPrice, outError) ||
			!reader.ReadRequiredUInt64(kSectionName, "MaximumListingPrice", loaded.maximumListingPrice, outError))
		{
			return false;
		}

		if (loaded.auctionPolicyId != 1 || loaded.maxActiveListings == 0 || loaded.maxActiveListings >= 100 || loaded.searchPageSize == 0 ||
			loaded.searchPageSize >= 100 || loaded.minimumListingDurationSeconds == 0 ||
			loaded.minimumListingDurationSeconds > loaded.maximumListingDurationSeconds ||
			loaded.defaultListingDurationSeconds < loaded.minimumListingDurationSeconds ||
			loaded.defaultListingDurationSeconds > loaded.maximumListingDurationSeconds || loaded.defaultCurrencyDataId == 0 ||
			loaded.defaultCurrencyDataId > std::numeric_limits<std::uint16_t>::max() || loaded.minimumBidIncrement == 0 ||
			loaded.minimumListingPrice == 0 || loaded.minimumListingPrice > loaded.maximumListingPrice ||
			loaded.minimumBidIncrement > loaded.maximumListingPrice)
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
