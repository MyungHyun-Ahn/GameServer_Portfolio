#pragma once

namespace Cache::Protocol
{
	struct FPlayerWorldSnapshot;
}

namespace GameData::Character
{
	class FCharacterDataTable;
}

namespace GameData::CharacterLevel
{
	class FCharacterLevelDataTable;
}

namespace GameData::Item
{
	class FItemDataTable;
}

namespace GameData::StatConversion
{
	class FStatConversionTable;
}

namespace WorldServer::Domain
{
	class FPlayerStatCalculator final
	{
	public:
		FPlayerStatCalculator(std::shared_ptr<const GameData::Character::FCharacterDataTable> characterDataTable,
			std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> characterLevelDataTable,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::StatConversion::FStatConversionTable> statConversionTable);

		[[nodiscard]] bool Calculate(const Cache::Protocol::FPlayerWorldSnapshot& source,
			WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
			std::string& outError) const;
		[[nodiscard]] bool BuildDevelopmentSnapshot(WorldCore::FUserId userId,
			WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
			std::string& outError) const;

	private:
		std::shared_ptr<const GameData::Character::FCharacterDataTable> m_characterDataTable;
		std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> m_characterLevelDataTable;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::StatConversion::FStatConversionTable> m_statConversionTable;
	};
}
