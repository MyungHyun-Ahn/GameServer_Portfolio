#pragma once

namespace GameData::Item
{
	class FItemDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SItemTemplate* Find(std::uint32_t itemDataId) const noexcept;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SItemTemplate> m_templates;
	};
}
