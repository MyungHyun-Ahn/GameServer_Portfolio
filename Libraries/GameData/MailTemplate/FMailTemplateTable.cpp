#include "GameDataPch.h"

#include "GameData/MailTemplate/FMailTemplateTable.h"

#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

namespace GameData::MailTemplate
{
	using GameData::Common::EMailTemplatePurpose;

	namespace
	{
		constexpr std::array<std::string_view, 5> kKnownKeys = {"MailTemplateDataId", "Purpose", "MailType", "Subject", "Body"};
		constexpr std::array<Foundation::Config::SConfigEnumValue<EMailTemplatePurpose>, 4> kPurposeValues = {
			{{"AuctionPurchase", EMailTemplatePurpose::AuctionPurchase},
				{"AuctionSaleProceeds", EMailTemplatePurpose::AuctionSaleProceeds},
				{"AuctionCancellationReturn", EMailTemplatePurpose::AuctionCancellationReturn},
				{"AuctionExpirationReturn", EMailTemplatePurpose::AuctionExpirationReturn}}};
	}

	bool FMailTemplateTable::Load(
		const std::filesystem::path& filePath,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document;
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		std::unordered_map<std::uint32_t, SMailTemplateData> loadedTemplates;
		std::unordered_map<EMailTemplatePurpose, std::uint32_t> loadedTemplateIdsByPurpose;
		Foundation::Config::FConfigValueReader reader(document);
		for (const auto& [sectionName, section] : document.sections)
		{
			(void)section;
			if (!reader.ValidateKnownKeys(sectionName, kKnownKeys, outError))
			{
				return false;
			}

			SMailTemplateData mailTemplate;
			if (!reader.ReadRequiredUInt32(sectionName, "MailTemplateDataId", mailTemplate.mailTemplateDataId, outError) ||
				!reader.ReadRequiredEnum(sectionName, "Purpose", kPurposeValues, mailTemplate.purpose, outError) ||
				!reader.ReadRequiredUInt32(sectionName, "MailType", mailTemplate.mailType, outError) ||
				!reader.ReadRequiredString(sectionName, "Subject", mailTemplate.subject, outError) ||
				!reader.ReadRequiredString(sectionName, "Body", mailTemplate.body, outError))
			{
				return false;
			}

			if (mailTemplate.mailTemplateDataId == 0 || mailTemplate.mailType == 0 || mailTemplate.mailType > 255 ||
				mailTemplate.subject.empty() || mailTemplate.subject.size() > 200 || mailTemplate.body.empty() ||
				mailTemplate.body.size() > 2000)
			{
				outError = "invalid mail template data in section: " + sectionName;
				return false;
			}

			const std::uint32_t mailTemplateDataId = mailTemplate.GetKey();
			if (!loadedTemplateIdsByPurpose.emplace(mailTemplate.purpose, mailTemplateDataId).second)
			{
				outError = "duplicate mail template Purpose in section: " + sectionName;
				return false;
			}
			if (!loadedTemplates.emplace(mailTemplateDataId, std::move(mailTemplate)).second)
			{
				outError = "duplicate MailTemplateDataId in section: " + sectionName;
				return false;
			}
		}

		if (loadedTemplates.empty())
		{
			outError = "mail template data table is empty.";
			return false;
		}
		if (loadedTemplateIdsByPurpose.size() != kPurposeValues.size())
		{
			outError = "mail template data must define every Purpose exactly once.";
			return false;
		}

		m_templates = std::move(loadedTemplates);
		m_templateIdsByPurpose = std::move(loadedTemplateIdsByPurpose);
		outError.clear();
		return true;
	}

	const SMailTemplateData* FMailTemplateTable::Find(
		const std::uint32_t mailTemplateDataId) const noexcept
	{
		const auto it = m_templates.find(mailTemplateDataId);
		return it == m_templates.end() ? nullptr : &it->second;
	}

	const SMailTemplateData* FMailTemplateTable::FindByPurpose(
		const EMailTemplatePurpose purpose) const noexcept
	{
		const auto it = m_templateIdsByPurpose.find(purpose);
		return it == m_templateIdsByPurpose.end() ? nullptr : Find(it->second);
	}

	std::size_t FMailTemplateTable::Size() const noexcept
	{
		return m_templates.size();
	}
}
