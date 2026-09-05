#pragma once

namespace GameData::Currency
{
	class FCurrencyDataTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SCurrencyData* Find(std::uint32_t currencyDataId) const noexcept;
		std::size_t Size() const noexcept;

	private:
		std::unordered_map<std::uint32_t, SCurrencyData> m_currencies;
	};
}
