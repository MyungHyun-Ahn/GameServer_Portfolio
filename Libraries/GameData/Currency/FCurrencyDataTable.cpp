#include "GameDataPch.h"

#include "GameData/Currency/FCurrencyDataTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::Currency
{
	namespace
	{
		constexpr std::array<std::string_view, 3> kKnownKeys = {"CurrencyDataId", "Name", "MaxAmount"};
	}

	bool FCurrencyDataTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SCurrencyData> loadedCurrencies;
		std::unordered_set<std::string> loadedCurrencyNames;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SCurrencyData currency;
			if (!reader.ReadRequiredUInt32(sectionName, "CurrencyDataId", currency.currencyDataId, outError) ||
				!reader.ReadRequiredString(sectionName, "Name", currency.name, outError) ||
				!reader.ReadRequiredUInt64(sectionName, "MaxAmount", currency.maxAmount, outError))
			{
				return false;
			}

			if (currency.currencyDataId == 0 || currency.currencyDataId > std::numeric_limits<std::uint16_t>::max() ||
				currency.name.empty() || currency.name.size() > 50 || currency.maxAmount == 0)
			{
				outError = "invalid currency data in section: " + sectionName;
				return false;
			}

			std::string normalizedName = currency.name;
			std::ranges::transform(normalizedName,
				normalizedName.begin(),
				[](const unsigned char value)
				{
					return static_cast<char>(std::tolower(value));
				});
			if (!loadedCurrencyNames.emplace(std::move(normalizedName)).second)
			{
				outError = "duplicate Currency Name in section: " + sectionName;
				return false;
			}

			const std::uint32_t currencyDataId = currency.GetKey();
			if (!loadedCurrencies.emplace(currencyDataId, std::move(currency)).second)
			{
				outError = "duplicate CurrencyDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedCurrencies.empty())
		{
			outError = "currency data table is empty.";
			return false;
		}

		m_currencies = std::move(loadedCurrencies);
		outError.clear();
		return true;
	}

	const SCurrencyData* FCurrencyDataTable::Find(
		const std::uint32_t currencyDataId) const noexcept
	{
		const auto it = m_currencies.find(currencyDataId);
		return it == m_currencies.end() ? nullptr : &it->second;
	}

	std::size_t FCurrencyDataTable::Size() const noexcept
	{
		return m_currencies.size();
	}
}
