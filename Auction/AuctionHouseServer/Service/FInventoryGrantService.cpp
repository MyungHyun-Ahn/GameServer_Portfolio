#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FInventoryGrantService.h"

#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"
#include "GameData/Item/FItemDataTable.h"

namespace AuctionHouseServer::Service
{
	FInventoryGrantService::FInventoryGrantService(
		Database::SAuctionDatabaseConfig databaseConfig,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable)
		: m_databaseConfig(std::move(databaseConfig))
		, m_itemDataTable(std::move(itemDataTable))
	{
	}

	Domain::EAuctionResultCode FInventoryGrantService::Execute(
		const std::uint64_t userId,
		const std::uint32_t itemDataId,
		const std::uint32_t quantity,
		Database::SInventoryItem& outItem,
		std::string& outError) const
	{
		if (userId == 0 || m_itemDataTable == nullptr)
		{
			outError = "invalid inventory grant context.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}
		const auto* itemData = m_itemDataTable->Find(itemDataId);
		if (itemData == nullptr || quantity == 0 || quantity > itemData->maxStack)
		{
			outError = "unknown ItemDataId or invalid quantity.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		auto* connection = context.GetGamePrimary(outError);
		if (connection == nullptr || !Database::FGameRepository(*connection)
										 .CreateInventoryItem(userId,
											 itemData->itemDataId,
											 quantity,
											 itemData->maxStack,
											 itemData->equipmentStats.str,
											 itemData->equipmentStats.dex,
											 itemData->equipmentStats.intelligence,
											 itemData->equipmentStats.luk,
											 itemData->tradable,
											 outItem,
											 outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		return Domain::EAuctionResultCode::Success;
	}
}
