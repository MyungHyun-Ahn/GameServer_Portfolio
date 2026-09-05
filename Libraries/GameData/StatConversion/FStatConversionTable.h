#pragma once

namespace GameData::StatConversion
{
	class FStatConversionTable final
	{
	public:
		bool Load(const std::filesystem::path& filePath, std::string& outError);
		const SStatConversionData* Find(std::uint32_t statConversionDataId) const noexcept;
		const SStatConversionData* Find(std::uint32_t characterDataId,
			GameData::Common::EPrimaryStatType sourceStat,
			GameData::Common::EDerivedStatType targetStat) const noexcept;
		std::vector<const SStatConversionData*> FindByCharacter(std::uint32_t characterDataId) const;
		std::size_t Size() const noexcept;

	private:
		static std::uint64_t MakeConversionKey(std::uint32_t characterDataId,
			GameData::Common::EPrimaryStatType sourceStat,
			GameData::Common::EDerivedStatType targetStat) noexcept;

		std::unordered_map<std::uint32_t, SStatConversionData> m_conversions;
		std::unordered_map<std::uint64_t, std::uint32_t> m_conversionIdsByKey;
	};
}
