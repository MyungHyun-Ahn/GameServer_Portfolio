#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBidService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"

namespace AuctionHouseServer::Service
{
	namespace
	{
		Domain::EAuctionResultCode MapPrepareError(
			const std::string& error)
		{
			if (error.find("BID_TOO_LOW") != std::string::npos)
			{
				return Domain::EAuctionResultCode::BidTooLow;
			}
			if (error.find("SELLER_CANNOT_BID") != std::string::npos)
			{
				return Domain::EAuctionResultCode::SellerCannotBid;
			}
			if (error.find("LISTING_VERSION_MISMATCH") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ListingVersionMismatch;
			}
			if (error.find("BID_STATE_INVALID") != std::string::npos)
			{
				return Domain::EAuctionResultCode::BidStateInvalid;
			}
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
	}

	FBidService::FBidService(
		Database::SAuctionDatabaseConfig config)
		: m_config(std::move(config))
	{
	}

	Domain::EAuctionResultCode FBidService::Execute(
		const std::uint64_t bidderUserId,
		const std::uint64_t listingId,
		const std::uint64_t bidAmount,
		const std::uint64_t expectedListingVersion,
		Database::SBidResult& outResult,
		std::string& outError) const
	{
		if (bidderUserId == 0 || listingId == 0 || bidAmount == 0 || expectedListingVersion == 0)
		{
			outError = "invalid bid request.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		auto* gameConnection = context.GetGamePrimary(outError);
		if (auctionConnection == nullptr || gameConnection == nullptr)
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Database::FGameRepository gameRepository(*gameConnection);
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		Database::SBidPrepareResult prepared;
		if (!auctionRepository.PrepareBid(listingId, bidderUserId, bidAmount, expectedListingVersion, prepared, outError))
		{
			return MapPrepareError(outError);
		}

		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		std::uint64_t currencyBalance = 0;
		if (!gameRepository.DebitCurrency(bidderUserId, prepared.currencyId, prepared.additionalDebit, currencyBalance, outError))
		{
			return outError.find("INSUFFICIENT_CURRENCY") != std::string::npos ? Domain::EAuctionResultCode::InsufficientCurrency
																			   : Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		if (!auctionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!gameTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		Connector::MySql::FMySqlTransaction completionTransaction(*auctionConnection);
		if (!completionTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		std::uint64_t listingVersion = 0;
		if (!auctionRepository.CompleteBid(
				listingId, prepared.bidId, bidderUserId, prepared.preparedListingVersion, listingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outResult.bidId = prepared.bidId;
		outResult.bidAmount = bidAmount;
		outResult.additionalDebit = prepared.additionalDebit;
		outResult.currencyBalance = currencyBalance;
		outResult.listingVersion = listingVersion;
		outResult.previousHighestBidId = prepared.previousHighestBidId;
		outResult.previousHighestBidderUserId = prepared.previousHighestBidderUserId;
		outResult.previousHighestAmount = prepared.previousHighestAmount;
		outError.clear();
		return Domain::EAuctionResultCode::Success;
	}
}
