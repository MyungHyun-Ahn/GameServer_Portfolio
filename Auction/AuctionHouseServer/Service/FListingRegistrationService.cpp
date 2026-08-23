#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FListingRegistrationService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "GameData/Item/FItemDataTable.h"

namespace AuctionHouseServer::Service
{
	namespace
	{
		void SetFailure(
			SListingRegistrationFailureContext& outFailure,
			const std::string_view failedStep,
			const std::string_view auctionDbCommit,
			const std::string_view gameDbCommit,
			const std::string_view remainingListingState,
			const bool faultInjected = false)
		{
			outFailure.failedStep = failedStep;
			outFailure.auctionDbCommit = auctionDbCommit;
			outFailure.gameDbCommit = gameDbCommit;
			outFailure.remainingListingState = remainingListingState;
			outFailure.faultInjected = faultInjected;
		}

		Domain::EAuctionResultCode MapInventoryError(
			const std::string& error)
		{
			if (error.find("INVENTORY_ITEM_NOT_FOUND") != std::string::npos)
			{
				return Domain::EAuctionResultCode::InventoryItemNotFound;
			}
			if (error.find("ITEM_VERSION_MISMATCH") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ItemVersionMismatch;
			}
			if (error.find("ITEM_EQUIPPED") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ItemEquipped;
			}
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Domain::EAuctionResultCode MapListingPrepareError(
			const std::string& error)
		{
			if (error.find("LISTING_LIMIT_EXCEEDED") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ListingLimitExceeded;
			}
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
	}

	FListingRegistrationService::FListingRegistrationService(
		Database::SAuctionDatabaseConfig databaseConfig,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
		const bool faultInjectionAfterAuctionCommit)
		: m_databaseConfig(std::move(databaseConfig))
		, m_itemDataTable(std::move(itemDataTable))
		, m_auctionPolicyTable(std::move(auctionPolicyTable))
		, m_faultInjectionAfterAuctionCommit(faultInjectionAfterAuctionCommit)
	{
	}

	Domain::EAuctionResultCode FListingRegistrationService::Execute(
		const std::uint64_t sellerUserId,
		const std::string_view sellerLoginId,
		const std::uint64_t itemInstanceId,
		const std::uint64_t expectedItemVersion,
		const std::uint16_t currencyId,
		const std::uint64_t startPrice,
		const std::uint64_t buyoutPrice,
		const std::uint32_t durationSeconds,
		std::uint64_t& outListingId,
		SListingRegistrationFailureContext& outFailure,
		std::string& outError) const
	{
		outListingId = 0;
		outFailure = {};
		if (m_auctionPolicyTable == nullptr)
		{
			outError = "auction policy is unavailable.";
			SetFailure(outFailure, "AuctionPolicy.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InternalError;
		}
		const auto& policy = m_auctionPolicyTable->Get();
		if (sellerUserId == 0 || sellerLoginId.empty() || itemInstanceId == 0 || expectedItemVersion == 0 || currencyId == 0 ||
			startPrice == 0 || (buyoutPrice != 0 && buyoutPrice < startPrice) || durationSeconds < policy.minimumListingDurationSeconds ||
			durationSeconds > policy.maximumListingDurationSeconds || m_itemDataTable == nullptr)
		{
			outError = "invalid listing registration request.";
			SetFailure(outFailure, "Request.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InvalidRequest;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		auto* gameConnection = context.GetGamePrimary(outError);
		if (gameConnection == nullptr)
		{
			SetFailure(outFailure, "GameDB.Connect", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
		{
			SetFailure(outFailure, "AuctionDB.Connect", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			SetFailure(outFailure, "GameDB.Begin", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		Database::SInventoryItem inventoryItem;
		if (!Database::FGameRepository(*gameConnection)
				.RemoveInventoryItem(sellerUserId, itemInstanceId, expectedItemVersion, inventoryItem, outError))
		{
			SetFailure(outFailure, "GameDB.RemoveInventoryItem", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return MapInventoryError(outError);
		}

		const auto* itemData = m_itemDataTable->Find(inventoryItem.itemDataId);
		if (itemData == nullptr || inventoryItem.quantity == 0 || inventoryItem.quantity > itemData->maxStack)
		{
			outError = "inventory item does not match ItemData.";
			SetFailure(outFailure, "ItemData.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InvalidRequest;
		}
		if (!itemData->tradable || !inventoryItem.tradable)
		{
			outError = "item is not tradable.";
			SetFailure(outFailure, "ItemTrade.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::ItemNotTradable;
		}

		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
		{
			SetFailure(outFailure, "AuctionDB.Begin", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Database::SListingPrepareRequest prepareRequest;
		prepareRequest.sellerUserId = sellerUserId;
		prepareRequest.sellerLoginId = sellerLoginId;
		prepareRequest.itemInstanceId = inventoryItem.itemInstanceId;
		prepareRequest.itemDataId = inventoryItem.itemDataId;
		prepareRequest.itemCategory = static_cast<std::uint8_t>(itemData->category);
		prepareRequest.quantity = inventoryItem.quantity;
		prepareRequest.itemDataJson = inventoryItem.itemDataJson;
		prepareRequest.searchName = itemData->name;
		prepareRequest.searchStr = inventoryItem.str;
		prepareRequest.searchDex = inventoryItem.dex;
		prepareRequest.searchInt = inventoryItem.intelligence;
		prepareRequest.searchLuk = inventoryItem.luk;
		prepareRequest.currencyId = currencyId;
		prepareRequest.startPrice = startPrice;
		prepareRequest.buyoutPrice = buyoutPrice;
		prepareRequest.durationSeconds = durationSeconds;
		prepareRequest.maxActiveListings = policy.maxActiveListings;

		Database::SListingPrepareResult prepareResult;
		Database::FAuctionRepository auctionRepository(*auctionConnection);
		if (!auctionRepository.PrepareListing(prepareRequest, prepareResult, outError))
		{
			SetFailure(outFailure, "AuctionDB.PrepareListing", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return MapListingPrepareError(outError);
		}

		outListingId = prepareResult.listingId;
		if (!auctionTransaction.Commit(outError))
		{
			SetFailure(outFailure, "AuctionDB.Commit", "UNKNOWN", "NOT_ATTEMPTED", "UNKNOWN");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		if (m_faultInjectionAfterAuctionCommit)
		{
			outError = "fault injection requested after AuctionDB commit.";
			SetFailure(outFailure, "BeforeGameDB.Commit", "SUCCEEDED", "NOT_ATTEMPTED", "REGISTER_PENDING", true);
			return Domain::EAuctionResultCode::PartialCommit;
		}

		if (!gameTransaction.Commit(outError))
		{
			SetFailure(outFailure, "GameDB.Commit", "SUCCEEDED", "UNKNOWN", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		Connector::MySql::FMySqlTransaction activationTransaction(*auctionConnection);
		if (!activationTransaction.Begin(outError))
		{
			SetFailure(outFailure, "AuctionDB.Activation.Begin", "SUCCEEDED", "SUCCEEDED", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!auctionRepository.ActivateListing(prepareResult.listingId, prepareResult.version, outError))
		{
			SetFailure(outFailure, "AuctionDB.ActivateListing", "SUCCEEDED", "SUCCEEDED", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!activationTransaction.Commit(outError))
		{
			SetFailure(outFailure, "AuctionDB.Activation.Commit", "SUCCEEDED", "SUCCEEDED", "REGISTER_PENDING_OR_ACTIVE");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outError.clear();
		return Domain::EAuctionResultCode::Success;
	}
}
