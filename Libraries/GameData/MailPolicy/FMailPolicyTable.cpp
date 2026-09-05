#include "GameDataPch.h"

#include "GameData/MailPolicy/FMailPolicyTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::MailPolicy
{
	namespace
	{
		constexpr std::string_view kSectionName = "MailPolicy1";
		constexpr std::array<std::string_view, 1> kKnownSections = {kSectionName};
		constexpr std::array<std::string_view, 3> kKnownKeys = {"MailPolicyId", "MailListPageSize", "ExpirationSeconds"};
	}

	bool FMailPolicyTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);
		SMailPolicyData loaded;
		if (!reader.ValidateKnownSections(kKnownSections, outError) || !reader.ValidateKnownKeys(kSectionName, kKnownKeys, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MailPolicyId", loaded.mailPolicyId, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "MailListPageSize", loaded.mailListPageSize, outError) ||
			!reader.ReadRequiredUInt32(kSectionName, "ExpirationSeconds", loaded.expirationSeconds, outError))
		{
			return false;
		}

		if (loaded.mailPolicyId != 1 || loaded.mailListPageSize == 0 || loaded.mailListPageSize >= 100 || loaded.expirationSeconds == 0)
		{
			outError = "invalid mail policy range.";
			return false;
		}

		m_policy = loaded;
		outError.clear();
		return true;
	}

	const SMailPolicyData& FMailPolicyTable::Get() const noexcept
	{
		return m_policy;
	}
}
