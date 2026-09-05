#pragma once

namespace GameData::MailPolicy
{
	class FMailPolicyTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SMailPolicyData& Get() const noexcept;

	private:
		SMailPolicyData m_policy;
	};
}
