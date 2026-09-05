#pragma once

namespace GameData::MailTemplate
{
	class FMailTemplateTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SMailTemplateData* Find(std::uint32_t mailTemplateDataId) const noexcept;
		const SMailTemplateData* FindByPurpose(GameData::Common::EMailTemplatePurpose purpose) const noexcept;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SMailTemplateData> m_templates;
		std::unordered_map<GameData::Common::EMailTemplatePurpose, std::uint32_t> m_templateIdsByPurpose;
	};
}
