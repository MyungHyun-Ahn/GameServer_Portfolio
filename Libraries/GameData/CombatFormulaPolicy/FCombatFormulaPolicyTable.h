#pragma once

namespace GameData::CombatFormulaPolicy
{
	class FCombatFormulaPolicyTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SCombatFormulaPolicyData& Get() const noexcept;

	private:
		SCombatFormulaPolicyData m_policy;
	};
}
