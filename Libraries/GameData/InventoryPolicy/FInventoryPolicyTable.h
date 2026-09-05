#pragma once

namespace GameData::InventoryPolicy
{
	class FInventoryPolicyTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SInventoryPolicyData& Get() const noexcept;

	private:
		SInventoryPolicyData m_policy;
	};
}
