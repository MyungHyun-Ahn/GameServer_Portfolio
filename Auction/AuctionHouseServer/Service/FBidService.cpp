#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBidService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"

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
			if (error.find("BID_REQUIRES_BUYOUT") != std::string::npos)
			{
				return Domain::EAuctionResultCode::BidRequiresBuyout;
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
		Database::SAuctionDatabaseConfig config,
		const std::uint64_t minimumBidIncrement)
		: m_config(std::move(config))
		, m_minimumBidIncrement(minimumBidIncrement)
	{
	}

	Domain::EAuctionResultCode FBidService::Prepare(
		const std::uint64_t bidderUserId,
		const std::uint64_t listingId,
		const std::uint64_t bidAmount,
		const std::uint64_t expectedListingVersion,
		Database::SBidPrepareResult& outResult,
		std::string& outError) const
	{
		if (bidderUserId == 0 || listingId == 0 || bidAmount == 0 || expectedListingVersion == 0)
		{
			outError = "invalid bid request.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}
		if (m_minimumBidIncrement == 0)
		{
			outError = "auction policy MinimumBidIncrement must be greater than zero.";
			return Domain::EAuctionResultCode::InternalError;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!auctionRepository.PrepareBid(
				listingId, bidderUserId, bidAmount, expectedListingVersion, m_minimumBidIncrement, outResult, outError))
		{
			auctionTransaction.Rollback();
			return MapPrepareError(outError);
		}
		if (!auctionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}

	Domain::EAuctionResultCode FBidService::Complete(
		const std::uint64_t bidderUserId,
		const std::uint64_t listingId,
		const std::uint64_t bidAmount,
		const std::uint64_t preparedListingVersion,
		std::uint64_t& outBidId,
		std::uint64_t& outListingVersion,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Connector::MySql::FMySqlTransaction completionTransaction(*auctionConnection);
		if (!completionTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!auctionRepository.CompleteBid(
				listingId, bidderUserId, bidAmount, preparedListingVersion, outBidId, outListingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		outError.clear();
		return Domain::EAuctionResultCode::Success;
	}

	bool FBidService::Revert(
		const std::uint64_t listingId,
		const std::uint64_t preparedListingVersion,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* connection = context.GetAuctionPrimary(outError);
		if (connection == nullptr)
		{
			return false;
		}
		Connector::MySql::FMySqlTransaction transaction(*connection);
		if (!transaction.Begin(outError))
		{
			return false;
		}
		if (!Database::FAuctionRepository(*connection).RevertBid(listingId, preparedListingVersion, outError))
		{
			transaction.Rollback();
			return false;
		}
		return transaction.Commit(outError);
	}
}
