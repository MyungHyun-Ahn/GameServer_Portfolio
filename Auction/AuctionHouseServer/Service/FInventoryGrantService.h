#pragma once

namespace GameData::Item
{
	class FItemDataTable;
}

namespace AuctionHouseServer::Service
{
	class FInventoryGrantService final
	{
	public:
		FInventoryGrantService(Database::SAuctionDatabaseConfig databaseConfig,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable);

		Domain::EAuctionResultCode Execute(std::uint64_t userId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			Database::SInventoryItem& outItem,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
	};
}
