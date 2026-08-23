#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FExpirationService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"

namespace AuctionHouseServer::Service
{
	FExpirationService::FExpirationService(
		Database::SAuctionDatabaseConfig config)
		: m_config(std::move(config))
	{
	}

	bool FExpirationService::GetCandidates(
		const std::uint32_t limit,
		std::vector<std::uint64_t>& outListingIds,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* connection = context.GetAuctionPrimary(outError);
		return connection != nullptr &&
			   Database::FAuctionRepository(*connection).GetExpiredListingCandidates(limit, outListingIds, outError);
	}

	Domain::EAuctionResultCode FExpirationService::Execute(
		const std::uint64_t listingId,
		Database::SExpirationResult& outResult,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		auto* gameConnection = context.GetGamePrimary(outError);
		if (auctionConnection == nullptr || gameConnection == nullptr)
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
			return Domain::EAuctionResultCode::DatabaseUnavailable;

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Database::SExpirationPrepareResult prepared;
		if (!auctionRepository.PrepareExpiration(listingId, prepared, outError))
		{
			return outError.find("EXPIRE_NOT_AVAILABLE") != std::string::npos ? Domain::EAuctionResultCode::ExpireNotAvailable
																			  : Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Database::FGameRepository gameRepository(*gameConnection);
		std::uint64_t itemMailId = 0;
		std::uint64_t sellerMailId = 0;
		const bool mailCreated = prepared.winnerUserId == 0
									 ? gameRepository.CreateExpiredItemReturnMail(prepared.sellerUserId,
										   prepared.itemInstanceId,
										   prepared.itemDataId,
										   prepared.quantity,
										   prepared.itemDataJson,
										   itemMailId,
										   outError)
									 : gameRepository.CreateItemMail(prepared.winnerUserId,
										   prepared.itemInstanceId,
										   prepared.itemDataId,
										   prepared.quantity,
										   prepared.itemDataJson,
										   itemMailId,
										   outError) &&
										   gameRepository.CreateCurrencyMail(
											   prepared.sellerUserId, prepared.currencyId, prepared.finalPrice, sellerMailId, outError);
		if (!mailCreated)
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!auctionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!gameTransaction.Commit(outError))
			return Domain::EAuctionResultCode::PartialCommit;
		Connector::MySql::FMySqlTransaction completionTransaction(*auctionConnection);
		if (!completionTransaction.Begin(outError))
			return Domain::EAuctionResultCode::PartialCommit;
		std::uint64_t listingVersion = 0;
		if (!auctionRepository.CompleteExpiration(
				listingId, prepared.winnerUserId, prepared.finalPrice, prepared.preparedListingVersion, listingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outResult.listingId = listingId;
		outResult.highestBidId = prepared.highestBidId;
		outResult.winnerUserId = prepared.winnerUserId;
		outResult.finalPrice = prepared.finalPrice;
		outResult.itemMailId = itemMailId;
		outResult.sellerMailId = sellerMailId;
		outResult.listingVersion = listingVersion;
		return Domain::EAuctionResultCode::Success;
	}
}
