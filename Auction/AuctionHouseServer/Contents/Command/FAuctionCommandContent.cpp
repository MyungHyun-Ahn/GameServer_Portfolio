#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Command/FAuctionCommandContent.h"

#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"
#include "AuctionHouseServer/Domain/AuctionResultCode.h"
#include "AuctionHouseServer/Service/FBidRefundService.h"
#include "AuctionHouseServer/Service/FBidService.h"
#include "AuctionHouseServer/Service/FBuyoutService.h"
#include "AuctionHouseServer/Service/FMailClaimService.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "AuctionHouseServer/Service/FListingCancelService.h"
#include "AuctionHouseServer/Service/FListingRegistrationService.h"
#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Auction/AuctionPackets.h"
#include "GameData/Item/FItemDataTable.h"

#include <Windows.h>

#include <format>
namespace AuctionHouseServer::Contents
{
	namespace
	{
		std::string_view GetResultCodeName(
			const Domain::EAuctionResultCode resultCode) noexcept
		{
			switch (resultCode)
			{
				case Domain::EAuctionResultCode::InvalidRequest:
					return "INVALID_REQUEST";
				case Domain::EAuctionResultCode::DatabaseUnavailable:
					return "DATABASE_UNAVAILABLE";
				case Domain::EAuctionResultCode::PartialCommit:
					return "PARTIAL_COMMIT";
				case Domain::EAuctionResultCode::InternalError:
					return "INTERNAL_ERROR";
				case Domain::EAuctionResultCode::InventoryItemNotFound:
					return "INVENTORY_ITEM_NOT_FOUND";
				case Domain::EAuctionResultCode::ItemNotTradable:
					return "ITEM_NOT_TRADABLE";
				case Domain::EAuctionResultCode::ItemVersionMismatch:
					return "ITEM_VERSION_MISMATCH";
				case Domain::EAuctionResultCode::ItemEquipped:
					return "ITEM_EQUIPPED";
				case Domain::EAuctionResultCode::ListingLimitExceeded:
					return "LISTING_LIMIT_EXCEEDED";
				default:
					return "AUCTION_ERROR";
			}
		}

		std::uint64_t GetUnixTimeMilliseconds() noexcept
		{
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	FAuctionCommandContent::FAuctionCommandContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const std::uint32_t shardIndex,
		const std::uint32_t shardCount,
		const std::uint64_t maxPacketQueueDepth,
		const std::uint32_t testDelayShardIndex,
		const std::uint32_t testDelayMilliseconds,
		const bool faultInjectionAfterAuctionCommit,
		std::shared_ptr<FAuctionUserRegistry> userRegistry,
		std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
		Database::SAuctionDatabaseConfig databaseConfig)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_shardIndex(shardIndex)
		, m_shardCount(shardCount)
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
		, m_testDelayShardIndex(testDelayShardIndex)
		, m_testDelayMilliseconds(testDelayMilliseconds)
		, m_faultInjectionAfterAuctionCommit(faultInjectionAfterAuctionCommit)
		, m_userRegistry(std::move(userRegistry))
		, m_itemDataTable(std::move(itemDataTable))
		, m_auctionPolicyTable(std::move(auctionPolicyTable))
		, m_databaseConfig(std::move(databaseConfig))
	{
	}

	ContentsRuntime::Core::FContentId FAuctionCommandContent::GetContentId() const noexcept
	{
		return kCommandContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuctionCommandContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint64_t FAuctionCommandContent::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FAuctionCommandContent::OnEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		Log(Foundation::ELogLevel::Info,
			"session entered command content. sessionId={} routeGeneration={} contentInstanceId={}",
			sessionId,
			routeGeneration,
			m_contentInstanceId);
	}

	void FAuctionCommandContent::OnLeave(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		Log(Foundation::ELogLevel::Info,
			"session left command content. sessionId={} routeGeneration={} contentInstanceId={}",
			sessionId,
			routeGeneration,
			m_contentInstanceId);
	}

	void FAuctionCommandContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (opcode == Generated::Auction::FPingRq::kOpcode)
		{
			HandlePing(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMyBidListRq::kOpcode)
		{
			HandleMyBidList(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FInventoryListRq::kOpcode)
		{
			HandleInventoryList(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingRegisterRq::kOpcode)
		{
			HandleListingRegister(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingSearchRq::kOpcode)
		{
			HandleListingSearch(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingDetailRq::kOpcode)
		{
			HandleListingDetail(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBidRq::kOpcode)
		{
			HandleBid(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBuyoutRq::kOpcode)
		{
			HandleBuyout(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailListRq::kOpcode)
		{
			HandleMailList(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailDetailRq::kOpcode)
		{
			HandleMailDetail(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FMailClaimRq::kOpcode)
		{
			HandleMailClaim(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FListingCancelRq::kOpcode)
		{
			HandleListingCancel(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FBidRefundRq::kOpcode)
		{
			HandleBidRefund(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FSaleHistorySearchRq::kOpcode)
		{
			HandleSaleHistorySearch(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FSaleHistoryDetailRq::kOpcode)
		{
			HandleSaleHistoryDetail(sessionId, payload, bridge);
			return;
		}
		if (opcode == Generated::Auction::FDebugCheatRq::kOpcode)
		{
			HandleDebugCheat(sessionId, payload, bridge);
			return;
		}

		Log(Foundation::ELogLevel::Warn, "unhandled command packet. sessionId={} opcode={}", sessionId, opcode);
	}

	void FAuctionCommandContent::HandleListingSearch(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FListingSearchRq request;
		const std::uint32_t maximumFetchSize = m_auctionPolicyTable->Get().searchPageSize + 1;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingSearchRq::kOpcode, payload, request) ||
			request.itemCategory > 3 || request.itemDataIds.size() > 100 || request.sellerOnly > 1 ||
			!Domain::IsValidListingSearchSortType(request.sortType) || request.limit == 0 || request.limit > maximumFetchSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingSearch request.");
			return;
		}

		Generated::Auction::FListingSearchRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		Database::SListingSearchQuery query;
		query.itemCategory = request.itemCategory;
		query.itemDataIds = request.itemDataIds;
		query.minStr = request.minStr;
		query.minDex = request.minDex;
		query.minInt = request.minInt;
		query.minLuk = request.minLuk;
		query.sellerUserId = request.sellerOnly != 0 ? *authenticatedUserId : 0;
		query.sortType = request.sortType;
		query.cursorSortValue = request.cursorSortValue;
		query.cursorListingId = request.cursorListingId;
		query.limit = request.limit;

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SListingSummary> listings;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).SearchListings(query, listings, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "ListingSearch failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& listing : listings)
			{
				response.listingIds.push_back(listing.listingId);
				response.sellerUserIds.push_back(listing.sellerUserId);
				response.sellerLoginIds.push_back(listing.sellerLoginId);
				response.itemDataIds.push_back(listing.itemDataId);
				response.itemCategories.push_back(listing.itemCategory);
				response.quantities.push_back(listing.quantity);
				response.names.push_back(listing.name);
				response.strStats.push_back(listing.str);
				response.dexStats.push_back(listing.dex);
				response.intStats.push_back(listing.intelligence);
				response.lukStats.push_back(listing.luk);
				response.currencyIds.push_back(listing.currencyId);
				response.startPrices.push_back(listing.startPrice);
				response.currentBidPrices.push_back(listing.currentBidPrice);
				response.buyoutPrices.push_back(listing.buyoutPrice);
				response.expiresAtUnixMs.push_back(listing.expiresAtUnixMs);
				response.versions.push_back(listing.version);
			}
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "ListingSearch response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"ListingSearch completed. userId={} itemCategory={} itemDataIdCount={} sortType={} count={} source={} workerNumber={} "
				"replicaNumber={}",
				*authenticatedUserId,
				static_cast<unsigned int>(request.itemCategory),
				request.itemDataIds.size(),
				static_cast<unsigned int>(request.sortType),
				listings.size(),
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleDebugCheat(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FDebugCheatRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FDebugCheatRq::kOpcode, payload, request) ||
			(request.cheatType != 1 && request.cheatType != 2))
		{
			Log(Foundation::ELogLevel::Warn, "invalid DebugCheat request.");
			return;
		}

		Generated::Auction::FDebugCheatRp response;
		response.requestId = request.requestId;
		response.cheatType = request.cheatType;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			response.message = "authentication required";
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		auto* connection = context.GetGamePrimary(error);
		if (connection == nullptr)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			response.message = error;
		}
		else if (request.cheatType == 1)
		{
			if (request.amount == 0 ||
				!Database::FGameRepository(*connection)
					.CreditCurrency(
						*authenticatedUserId, 1, request.amount, m_databaseConfig.maxCurrencyAmount, response.currencyBalance, error))
			{
				response.resultCode = static_cast<std::uint16_t>(
					request.amount == 0 ? Domain::EAuctionResultCode::InvalidRequest : Domain::EAuctionResultCode::DatabaseUnavailable);
				response.message = request.amount == 0 ? "gold amount must be greater than zero" : error;
			}
			else
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
				response.message = std::format("gold credited: {}", request.amount);
			}
		}
		else
		{
			const auto* itemData = m_itemDataTable != nullptr ? m_itemDataTable->Find(request.itemDataId) : nullptr;
			Database::SInventoryItem item;
			if (itemData == nullptr)
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::InvalidRequest);
				response.message = "unknown ItemDataId";
			}
			else if (!Database::FGameRepository(*connection)
						 .CreateInventoryItem(*authenticatedUserId,
							 request.itemDataId,
							 1,
							 itemData->maxStack,
							 request.strStat,
							 request.dexStat,
							 request.intStat,
							 request.lukStat,
							 itemData->tradable,
							 item,
							 error))
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
				response.message = error;
			}
			else
			{
				response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
				response.itemInstanceId = item.itemInstanceId;
				response.message = std::format("item created: ItemDataId={} instanceId={}", request.itemDataId, item.itemInstanceId);
			}
		}

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
			Log(Foundation::ELogLevel::Error, "DebugCheat response send failed.");
		else
			Log(Foundation::ELogLevel::Info,
				"DebugCheat completed. userId={} cheatType={} resultCode={}",
				*authenticatedUserId,
				static_cast<unsigned int>(request.cheatType),
				response.resultCode);
	}

	void FAuctionCommandContent::HandleListingDetail(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FListingDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingDetailRq::kOpcode, payload, request) ||
			request.listingId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingDetail request.");
			return;
		}

		Generated::Auction::FListingDetailRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Database::SListingDetail listing;
		bool found = false;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).GetListingDetail(request.listingId, listing, found, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "ListingDetail failed: " + error);
		}
		else if (!found)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ListingNotFound);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			response.sellerUserId = listing.sellerUserId;
			response.sellerLoginId = listing.sellerLoginId;
			response.itemInstanceId = listing.itemInstanceId;
			response.itemDataId = listing.itemDataId;
			response.itemCategory = listing.itemCategory;
			response.quantity = listing.quantity;
			response.itemData = listing.itemDataJson;
			response.name = listing.name;
			response.strStat = listing.str;
			response.dexStat = listing.dex;
			response.intStat = listing.intelligence;
			response.lukStat = listing.luk;
			response.currencyId = listing.currencyId;
			response.startPrice = listing.startPrice;
			response.currentBidPrice = listing.currentBidPrice;
			response.buyoutPrice = listing.buyoutPrice;
			response.highestBidderUserId = listing.highestBidderUserId;
			response.expiresAtUnixMs = listing.expiresAtUnixMs;
			response.version = listing.version;
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "ListingDetail response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"ListingDetail completed. userId={} listingId={} source={} workerNumber={} replicaNumber={}",
				*authenticatedUserId,
				request.listingId,
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleSaleHistorySearch(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FSaleHistorySearchRq request;
		const std::uint32_t maximumFetchSize = m_auctionPolicyTable->Get().searchPageSize + 1;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FSaleHistorySearchRq::kOpcode, payload, request) ||
			request.itemCategory > 3 || request.itemDataIds.size() > 100 || !Domain::IsValidSaleHistorySortType(request.sortType) ||
			request.limit == 0 || request.limit > maximumFetchSize)
		{
			Log(Foundation::ELogLevel::Warn, "invalid SaleHistorySearch request.");
			return;
		}

		Generated::Auction::FSaleHistorySearchRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		Database::SSaleHistoryQuery query;
		query.itemCategory = request.itemCategory;
		query.itemDataIds = request.itemDataIds;
		query.minStr = request.minStr;
		query.minDex = request.minDex;
		query.minInt = request.minInt;
		query.minLuk = request.minLuk;
		query.sortType = request.sortType;
		query.cursorSortValue = request.cursorSortValue;
		query.cursorListingId = request.cursorListingId;
		query.limit = request.limit;

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SSaleHistorySummary> history;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).SearchSaleHistory(query, history, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "SaleHistorySearch failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& sale : history)
			{
				response.listingIds.push_back(sale.listingId);
				response.itemDataIds.push_back(sale.itemDataId);
				response.itemCategories.push_back(sale.itemCategory);
				response.quantities.push_back(sale.quantity);
				response.names.push_back(sale.name);
				response.strStats.push_back(sale.str);
				response.dexStats.push_back(sale.dex);
				response.intStats.push_back(sale.intelligence);
				response.lukStats.push_back(sale.luk);
				response.currencyIds.push_back(sale.currencyId);
				response.finalPrices.push_back(sale.finalPrice);
				response.saleTypes.push_back(sale.saleType);
				response.soldAtUnixMs.push_back(sale.soldAtUnixMs);
			}
		}

		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"SaleHistorySearch completed. userId={} itemDataIdCount={} sortType={} count={} source={} workerNumber={} replicaNumber={}",
				*authenticatedUserId,
				request.itemDataIds.size(),
				static_cast<unsigned int>(request.sortType),
				history.size(),
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveAuctionReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleSaleHistoryDetail(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FSaleHistoryDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FSaleHistoryDetailRq::kOpcode, payload, request) ||
			request.listingId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid SaleHistoryDetail request.");
			return;
		}

		Generated::Auction::FSaleHistoryDetailRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Database::SSaleHistoryDetail sale;
		bool found = false;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection).GetSaleHistoryDetail(request.listingId, sale, found, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "SaleHistoryDetail failed: " + error);
		}
		else if (!found)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::ListingNotFound);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			response.sellerLoginId = sale.sellerLoginId;
			response.itemDataId = sale.itemDataId;
			response.itemCategory = sale.itemCategory;
			response.quantity = sale.quantity;
			response.itemData = sale.itemDataJson;
			response.name = sale.name;
			response.strStat = sale.str;
			response.dexStat = sale.dex;
			response.intStat = sale.intelligence;
			response.lukStat = sale.luk;
			response.currencyId = sale.currencyId;
			response.startPrice = sale.startPrice;
			response.finalPrice = sale.finalPrice;
			response.saleType = sale.saleType;
			response.soldAtUnixMs = sale.soldAtUnixMs;
		}
		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
	}

	void FAuctionCommandContent::HandleListingRegister(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FListingRegisterRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingRegisterRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingRegister request.");
			return;
		}

		Generated::Auction::FListingRegisterRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		const auto sellerLoginId = m_userRegistry->GetLoginId(sessionId);
		if (!authenticatedUserId.has_value() || !sellerLoginId.has_value() || sellerLoginId->empty())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		std::string error;
		Service::SListingRegistrationFailureContext failure;
		Service::FListingRegistrationService service(
			m_databaseConfig, m_itemDataTable, m_auctionPolicyTable, m_faultInjectionAfterAuctionCommit);
		const auto resultCode = service.Execute(*authenticatedUserId,
			*sellerLoginId,
			request.itemInstanceId,
			request.expectedItemVersion,
			request.currencyId,
			request.startPrice,
			request.buyoutPrice,
			request.durationSeconds,
			response.listingId,
			failure,
			error);
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "ListingRegister response send failed.");
		}
		if (resultCode != Domain::EAuctionResultCode::Success)
		{
			if (failure.HasFailure())
			{
				const auto level =
					resultCode == Domain::EAuctionResultCode::PartialCommit ? Foundation::ELogLevel::Error : Foundation::ELogLevel::Warn;
				Log(level,
					"Auction operation failed. operation=ListingRegister requestId={} userId={} itemInstanceId={} listingId={} "
					"result={}({}) failedStep={} auctionDbCommit={} gameDbCommit={} remainingState={} faultInjected={} error={}",
					request.requestId,
					*authenticatedUserId,
					request.itemInstanceId,
					response.listingId,
					GetResultCodeName(resultCode),
					static_cast<std::uint16_t>(resultCode),
					failure.failedStep,
					failure.auctionDbCommit,
					failure.gameDbCommit,
					failure.remainingListingState,
					failure.faultInjected,
					error);
			}
			else
			{
				Log(Foundation::ELogLevel::Warn, "ListingRegister failed: " + error);
			}
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"ListingRegister completed. userId={} itemInstanceId={} listingId={} shardIndex={}",
				*authenticatedUserId,
				request.itemInstanceId,
				response.listingId,
				m_shardIndex);
		}
	}

	void FAuctionCommandContent::HandleInventoryList(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FInventoryListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FInventoryListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > 100)
		{
			Log(Foundation::ELogLevel::Warn, "invalid InventoryList request.");
			return;
		}

		Generated::Auction::FInventoryListRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SInventoryItem> items;
		bool usedPrimary = false;
		if (!context.ExecuteGameReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FGameRepository(connection)
						.GetInventoryItems(*authenticatedUserId, request.cursorItemInstanceId, request.limit, items, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "InventoryList failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& item : items)
			{
				response.itemInstanceIds.push_back(item.itemInstanceId);
				response.itemDataIds.push_back(item.itemDataId);
				response.quantities.push_back(item.quantity);
				response.equippedStates.push_back(item.equipped ? 1 : 0);
				response.tradableStates.push_back(item.tradable ? 1 : 0);
				response.itemData.push_back(item.itemDataJson);
				response.versions.push_back(item.version);
			}
		}

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "InventoryList response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"InventoryList completed. userId={} shardIndex={} itemCount={} source={} workerNumber={} replicaNumber={}",
				*authenticatedUserId,
				m_shardIndex,
				items.size(),
				(usedPrimary ? "primary" : "replica"),
				context.GetWorkerIndex() + 1,
				(usedPrimary ? 0 : context.GetActiveGameReplicaIndex() + 1));
		}
	}

	void FAuctionCommandContent::HandleMyBidList(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FMyBidListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMyBidListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > 100)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MyBidList request.");
			return;
		}

		Generated::Auction::FMyBidListRp response;
		response.requestId = request.requestId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SMyBid> bids;
		bool usedPrimary = false;
		if (!context.ExecuteAuctionReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FAuctionRepository(connection)
						.GetMyBids(*authenticatedUserId, request.cursorBidId, request.limit, bids, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "MyBidList failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			for (const auto& bid : bids)
			{
				response.bidIds.push_back(bid.bidId);
				response.listingIds.push_back(bid.listingId);
				response.currencyIds.push_back(bid.currencyId);
				response.bidAmounts.push_back(bid.bidAmount);
				response.bidStates.push_back(bid.bidState);
				response.bidVersions.push_back(bid.bidVersion);
				response.currentBidPrices.push_back(bid.currentBidPrice);
				response.listingStates.push_back(bid.listingState);
			}
		}
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "MyBidList response send failed.");
		}
		else if (response.resultCode == static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success))
		{
			Log(Foundation::ELogLevel::Info,
				"MyBidList completed. userId={} shardIndex={} bidCount={} source={}",
				*authenticatedUserId,
				m_shardIndex,
				bids.size(),
				(usedPrimary ? "primary" : "replica"));
		}
	}

	void FAuctionCommandContent::HandleBid(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FBidRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBidRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid Bid request.");
			return;
		}

		Generated::Auction::FBidRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		Database::SBidResult bidResult;
		std::string error;
		Service::FBidService service(m_databaseConfig);
		const auto resultCode =
			service.Execute(*authenticatedUserId, request.listingId, request.bidAmount, request.expectedListingVersion, bidResult, error);
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		response.bidId = bidResult.bidId;
		response.bidAmount = bidResult.bidAmount;
		response.additionalDebit = bidResult.additionalDebit;
		response.currencyBalance = bidResult.currencyBalance;
		response.listingVersion = bidResult.listingVersion;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "Bid response send failed.");
		}

		if (resultCode != Domain::EAuctionResultCode::Success)
		{
			Log(Foundation::ELogLevel::Warn, "Bid failed: " + error);
			return;
		}

		if (bidResult.previousHighestBidderUserId != 0 && bidResult.previousHighestBidderUserId != *authenticatedUserId)
		{
			const auto previousSessionId = m_userRegistry->GetSessionId(bidResult.previousHighestBidderUserId);
			if (previousSessionId.has_value() && bridge.IsSessionAlive(*previousSessionId))
			{
				Generated::Auction::FAuctionOutbidNoti notification;
				notification.listingId = request.listingId;
				notification.bidId = bidResult.previousHighestBidId;
				notification.heldAmount = bidResult.previousHighestAmount;
				notification.newHighestAmount = request.bidAmount;
				if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *previousSessionId, notification))
				{
					Log(Foundation::ELogLevel::Warn, "online outbid notification send failed.");
				}
			}
		}

		Log(Foundation::ELogLevel::Info,
			"Bid completed. userId={} listingId={} bidId={} amount={} previousHighestUserId={}",
			*authenticatedUserId,
			request.listingId,
			bidResult.bidId,
			request.bidAmount,
			bidResult.previousHighestBidderUserId);
	}

	void FAuctionCommandContent::HandleBuyout(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FBuyoutRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBuyoutRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid Buyout request.");
			return;
		}

		Generated::Auction::FBuyoutRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}

		Database::SBuyoutResult buyoutResult;
		std::string error;
		Service::FBuyoutService service(m_databaseConfig);
		const auto resultCode =
			service.Execute(*authenticatedUserId, request.listingId, request.expectedListingVersion, buyoutResult, error);
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		response.buyoutPrice = buyoutResult.buyoutPrice;
		response.additionalDebit = buyoutResult.additionalDebit;
		response.currencyBalance = buyoutResult.currencyBalance;
		response.itemMailId = buyoutResult.itemMailId;
		response.sellerMailId = buyoutResult.sellerMailId;
		response.listingVersion = buyoutResult.listingVersion;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "Buyout response send failed.");
		}
		if (resultCode != Domain::EAuctionResultCode::Success)
		{
			Log(Foundation::ELogLevel::Warn, "Buyout failed: " + error);
			return;
		}

		if (buyoutResult.previousHighestBidderUserId != 0 && buyoutResult.previousHighestBidderUserId != *authenticatedUserId)
		{
			const auto previousSessionId = m_userRegistry->GetSessionId(buyoutResult.previousHighestBidderUserId);
			if (previousSessionId.has_value() && bridge.IsSessionAlive(*previousSessionId))
			{
				Generated::Auction::FAuctionOutbidNoti notification;
				notification.listingId = request.listingId;
				notification.bidId = buyoutResult.previousHighestBidId;
				notification.heldAmount = buyoutResult.previousHighestAmount;
				notification.newHighestAmount = buyoutResult.buyoutPrice;
				ContentsRuntime::Bridge::SendContentPacket(bridge, *previousSessionId, notification);
			}
		}

		Log(Foundation::ELogLevel::Info,
			"Buyout completed. userId={} listingId={} price={} itemMailId={} sellerMailId={}",
			*authenticatedUserId,
			request.listingId,
			buyoutResult.buyoutPrice,
			buyoutResult.itemMailId,
			buyoutResult.sellerMailId);
	}

	void FAuctionCommandContent::HandleMailList(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FMailListRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailListRq::kOpcode, payload, request) ||
			request.limit == 0 || request.limit > 100)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailList request.");
			return;
		}
		Generated::Auction::FMailListRp response;
		response.requestId = request.requestId;
		const auto userId = m_userRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		std::vector<Database::SMailSummary> mails;
		bool usedPrimary = false;
		if (!context.ExecuteGameReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FGameRepository(connection)
						.GetMailList(*userId, request.cursorMailId, request.limit, mails, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
			Log(Foundation::ELogLevel::Error, "MailList failed: " + error);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			if (usedPrimary)
				Log(Foundation::ELogLevel::Warn, "MailList replica unavailable; primary fallback succeeded.");
			for (const auto& mail : mails)
			{
				response.mailIds.push_back(mail.mailId);
				response.mailTypes.push_back(mail.mailType);
				response.subjects.push_back(mail.subject);
				response.states.push_back(mail.state);
				response.expiresAtUnixMs.push_back(mail.expiresAtUnixMs);
				response.createdAtUnixMs.push_back(mail.createdAtUnixMs);
			}
		}
		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
	}

	void FAuctionCommandContent::HandleMailDetail(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FMailDetailRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailDetailRq::kOpcode, payload, request) ||
			request.mailId == 0)
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailDetail request.");
			return;
		}
		Generated::Auction::FMailDetailRp response;
		response.requestId = request.requestId;
		response.mailId = request.mailId;
		const auto userId = m_userRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		std::string error;
		auto& context = Database::FContentThreadDbContext::Get(m_databaseConfig);
		Database::SMailDetail mail;
		bool found = false;
		bool usedPrimary = false;
		if (!context.ExecuteGameReadWithPrimaryFallback(
				false,
				[&](Connector::MySql::FMySqlConnection& connection, std::string& operationError)
				{
					return Database::FGameRepository(connection).GetMailDetail(*userId, request.mailId, mail, found, operationError);
				},
				usedPrimary,
				error))
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::DatabaseUnavailable);
		}
		else if (!found)
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::MailNotFound);
		}
		else
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
			if (usedPrimary)
				Log(Foundation::ELogLevel::Warn, "MailDetail replica unavailable; primary fallback succeeded.");
			response.mailType = mail.mailType;
			response.subject = mail.subject;
			response.body = mail.body;
			response.state = mail.state;
			response.expiresAtUnixMs = mail.expiresAtUnixMs;
			for (const auto& attachment : mail.attachments)
			{
				response.attachmentIds.push_back(attachment.attachmentId);
				response.attachmentTypes.push_back(attachment.attachmentType);
				response.itemInstanceIds.push_back(attachment.itemInstanceId);
				response.itemDataIds.push_back(attachment.itemDataId);
				response.quantities.push_back(attachment.quantity);
				response.itemData.push_back(attachment.itemDataJson);
				response.currencyIds.push_back(attachment.currencyId);
				response.currencyAmounts.push_back(attachment.currencyAmount);
				response.attachmentStates.push_back(attachment.state);
			}
		}
		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
	}

	void FAuctionCommandContent::HandleMailClaim(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FMailClaimRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FMailClaimRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid MailClaim request.");
			return;
		}
		Generated::Auction::FMailClaimRp response;
		response.requestId = request.requestId;
		response.mailId = request.mailId;
		response.attachmentId = request.attachmentId;
		const auto userId = m_userRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		Database::SMailClaimResult claim;
		std::string error;
		const auto resultCode =
			Service::FMailClaimService(m_databaseConfig).Execute(*userId, request.mailId, request.attachmentId, claim, error);
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		response.attachmentType = claim.attachmentType;
		response.itemInstanceId = claim.itemInstanceId;
		response.itemDataId = claim.itemDataId;
		response.quantity = claim.quantity;
		response.currencyId = claim.currencyId;
		response.currencyAmount = claim.currencyAmount;
		response.currencyBalance = claim.currencyBalance;
		response.mailState = claim.mailState;
		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		if (resultCode != Domain::EAuctionResultCode::Success)
		{
			Log(Foundation::ELogLevel::Warn, "MailClaim failed: " + error);
		}
	}

	void FAuctionCommandContent::HandleListingCancel(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FListingCancelRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FListingCancelRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid ListingCancel request.");
			return;
		}
		Generated::Auction::FListingCancelRp response;
		response.requestId = request.requestId;
		response.listingId = request.listingId;
		const auto userId = m_userRegistry->GetUserId(sessionId);
		if (!userId.has_value())
		{
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		Database::SListingCancelResult cancelResult;
		std::string error;
		const auto resultCode = Service::FListingCancelService(m_databaseConfig)
									.Execute(*userId, request.listingId, request.expectedListingVersion, cancelResult, error);
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		response.returnMailId = cancelResult.returnMailId;
		response.listingVersion = cancelResult.listingVersion;
		ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
		if (resultCode == Domain::EAuctionResultCode::Success)
		{
			Log(Foundation::ELogLevel::Info,
				"ListingCancel completed. userId={} listingId={} returnMailId={}",
				*userId,
				request.listingId,
				cancelResult.returnMailId);
		}
		else
		{
			Log(Foundation::ELogLevel::Warn, "ListingCancel failed: " + error);
		}
	}

	void FAuctionCommandContent::HandleBidRefund(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FBidRefundRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FBidRefundRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "invalid BidRefund request.");
			return;
		}

		Database::SBidRefundResult refundResult;
		const auto authenticatedUserId = m_userRegistry->GetUserId(sessionId);
		if (!authenticatedUserId.has_value())
		{
			Generated::Auction::FBidRefundRp response;
			response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::AuthRequired);
			response.requestId = request.requestId;
			response.bidId = request.bidId;
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			return;
		}
		std::string error;
		Service::FBidRefundService service(m_databaseConfig);
		const auto resultCode =
			service.Execute(*authenticatedUserId, request.listingId, request.bidId, request.expectedBidVersion, refundResult, error);

		Generated::Auction::FBidRefundRp response;
		response.resultCode = static_cast<std::uint16_t>(resultCode);
		response.requestId = request.requestId;
		response.bidId = request.bidId;
		response.refundedAmount = refundResult.refundedAmount;
		response.currencyBalance = refundResult.currencyBalance;
		response.bidState = refundResult.bidState;
		response.bidVersion = refundResult.bidVersion;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "BidRefund response send failed.");
		}
		if (resultCode != Domain::EAuctionResultCode::Success)
		{
			Log(Foundation::ELogLevel::Warn, "BidRefund failed: " + error);
		}
		else
		{
			Log(Foundation::ELogLevel::Info,
				"BidRefund completed. userId={} listingId={} shardIndex={}",
				*authenticatedUserId,
				request.listingId,
				m_shardIndex);
		}
	}

	void FAuctionCommandContent::HandlePing(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::Auction::FPingRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::Auction::FPingRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "ping request deserialize failed.");
			return;
		}

		if (m_shardIndex == m_testDelayShardIndex && m_testDelayMilliseconds > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(m_testDelayMilliseconds));
		}

		Generated::Auction::FPingRp response;
		response.resultCode = static_cast<std::uint16_t>(Domain::EAuctionResultCode::Success);
		response.requestId = request.requestId;
		response.routingKey = request.routingKey;
		response.clientTimeUnixMs = request.clientTimeUnixMs;
		response.serverTimeUnixMs = GetUnixTimeMilliseconds();
		response.shardIndex = m_shardIndex;
		response.shardCount = m_shardCount;
		response.contentInstanceId = m_contentInstanceId;
		response.contentThreadId = static_cast<std::uint32_t>(GetCurrentThreadId());

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "ping response send failed.");
			return;
		}

		Log(Foundation::ELogLevel::Info,
			"ping handled on command content. sessionId={} requestId={} routingKey={} shardIndex={} contentThreadId={}",
			sessionId,
			request.requestId,
			request.routingKey,
			m_shardIndex,
			response.contentThreadId);
	}

	void FAuctionCommandContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "AuctionHouseServer", message);
		}
	}
}
