#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FListingRegistrationService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
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
			const std::string_view cacheRpc,
			const std::string_view activationCommit,
			const std::string_view remainingListingState)
		{
			outFailure.failedStep = failedStep;
			outFailure.auctionDbCommit = auctionDbCommit;
			outFailure.cacheRpc = cacheRpc;
			outFailure.activationCommit = activationCommit;
			outFailure.remainingListingState = remainingListingState;
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
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable)
		: m_databaseConfig(std::move(databaseConfig))
		, m_itemDataTable(std::move(itemDataTable))
		, m_auctionPolicyTable(std::move(auctionPolicyTable))
	{
	}

	Domain::EAuctionResultCode FListingRegistrationService::Prepare(
		const std::uint64_t sellerUserId,
		const std::string_view sellerLoginId,
		const Database::SInventoryItem& inventoryItem,
		const std::uint64_t expectedItemVersion,
		const std::uint16_t currencyId,
		const std::uint64_t startPrice,
		const std::uint64_t buyoutPrice,
		const std::uint32_t durationSeconds,
		Database::SListingPrepareResult& outResult,
		SListingRegistrationFailureContext& outFailure,
		std::string& outError) const
	{
		outResult = {};
		outFailure = {};
		if (m_auctionPolicyTable == nullptr || m_itemDataTable == nullptr)
		{
			outError = "auction policy or item data is unavailable.";
			SetFailure(outFailure, "Policy.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InternalError;
		}

		const auto& policy = m_auctionPolicyTable->Get();
		if (policy.defaultCurrencyDataId > std::numeric_limits<std::uint16_t>::max())
		{
			outError = "auction policy DefaultCurrencyDataId exceeds the packet currencyId range.";
			SetFailure(outFailure, "Policy.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InternalError;
		}

		const auto defaultCurrencyId = static_cast<std::uint16_t>(policy.defaultCurrencyDataId);
		if (sellerUserId == 0 || sellerLoginId.empty() || inventoryItem.itemInstanceId == 0 || expectedItemVersion == 0 ||
			inventoryItem.version != expectedItemVersion || currencyId != defaultCurrencyId || startPrice < policy.minimumListingPrice ||
			startPrice > policy.maximumListingPrice ||
			(buyoutPrice != 0 && (buyoutPrice < startPrice || buyoutPrice > policy.maximumListingPrice)) ||
			durationSeconds < policy.minimumListingDurationSeconds || durationSeconds > policy.maximumListingDurationSeconds)
		{
			outError = "invalid listing registration request.";
			SetFailure(outFailure, "Request.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return inventoryItem.version != expectedItemVersion ? Domain::EAuctionResultCode::ItemVersionMismatch
																: Domain::EAuctionResultCode::InvalidRequest;
		}

		const auto* itemData = m_itemDataTable->Find(inventoryItem.itemDataId);
		if (itemData == nullptr || inventoryItem.quantity == 0 || inventoryItem.quantity > itemData->maxStack)
		{
			outError = "inventory item does not match ItemData.";
			SetFailure(outFailure, "ItemData.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::InvalidRequest;
		}
		if (inventoryItem.equipped)
		{
			outError = "equipped item cannot be listed.";
			SetFailure(outFailure, "ItemTrade.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::ItemEquipped;
		}
		if (!itemData->tradable || !inventoryItem.tradable)
		{
			outError = "item is not tradable.";
			SetFailure(outFailure, "ItemTrade.Validate", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::ItemNotTradable;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Connector::MySql::FMySqlConnection* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
		{
			SetFailure(outFailure, "AuctionDB.Connect", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Connector::MySql::FMySqlTransaction transaction(*auctionConnection);
		if (!transaction.Begin(outError))
		{
			SetFailure(outFailure, "AuctionDB.Begin", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
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

		if (!Database::FAuctionRepository(*auctionConnection).PrepareListing(prepareRequest, outResult, outError))
		{
			transaction.Rollback();
			SetFailure(outFailure, "AuctionDB.PrepareListing", "ROLLED_BACK", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "NONE");
			return MapListingPrepareError(outError);
		}
		if (!transaction.Commit(outError))
		{
			SetFailure(outFailure, "AuctionDB.Commit", "UNKNOWN", "NOT_ATTEMPTED", "NOT_ATTEMPTED", "UNKNOWN");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outFailure.auctionDbCommit = "SUCCEEDED";
		outFailure.remainingListingState = "REGISTER_PENDING";
		return Domain::EAuctionResultCode::Success;
	}

	Domain::EAuctionResultCode FListingRegistrationService::Activate(
		const std::uint64_t listingId,
		const std::uint64_t expectedVersion,
		SListingRegistrationFailureContext& outFailure,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Connector::MySql::FMySqlConnection* connection = context.GetAuctionPrimary(outError);
		if (connection == nullptr)
		{
			SetFailure(outFailure, "AuctionDB.Activation.Connect", "SUCCEEDED", "SUCCEEDED", "NOT_ATTEMPTED", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		Connector::MySql::FMySqlTransaction transaction(*connection);
		if (!transaction.Begin(outError))
		{
			SetFailure(outFailure, "AuctionDB.Activation.Begin", "SUCCEEDED", "SUCCEEDED", "NOT_ATTEMPTED", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!Database::FAuctionRepository(*connection).ActivateListing(listingId, expectedVersion, outError))
		{
			transaction.Rollback();
			SetFailure(outFailure, "AuctionDB.ActivateListing", "SUCCEEDED", "SUCCEEDED", "ROLLED_BACK", "REGISTER_PENDING");
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!transaction.Commit(outError))
		{
			SetFailure(outFailure, "AuctionDB.Activation.Commit", "SUCCEEDED", "SUCCEEDED", "UNKNOWN", "REGISTER_PENDING_OR_ACTIVE");
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outFailure.activationCommit = "SUCCEEDED";
		outFailure.remainingListingState = "ACTIVE";
		return Domain::EAuctionResultCode::Success;
	}

	bool FListingRegistrationService::DeletePending(
		const std::uint64_t sellerUserId,
		const std::uint64_t listingId,
		const std::uint64_t expectedVersion,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Connector::MySql::FMySqlConnection* connection = context.GetAuctionPrimary(outError);
		if (connection == nullptr)
		{
			return false;
		}

		Connector::MySql::FMySqlTransaction transaction(*connection);
		if (!transaction.Begin(outError) ||
			!Database::FAuctionRepository(*connection).DeletePendingListing(listingId, sellerUserId, expectedVersion, outError))
		{
			return false;
		}
		return transaction.Commit(outError);
	}
}
