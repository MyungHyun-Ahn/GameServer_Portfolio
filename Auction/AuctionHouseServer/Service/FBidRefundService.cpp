#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBidRefundService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
namespace AuctionHouseServer::Service
{
	FBidRefundService::FBidRefundService(
		const Database::SAuctionDatabaseConfig& config)
		: m_config(config)
	{
	}

	Domain::EAuctionResultCode FBidRefundService::Prepare(
		const std::uint64_t userId,
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t expectedBidVersion,
		Database::SBidRefundPrepareResult& outResult,
		std::string& outError)
	{
		if (userId == 0 || listingId == 0 || bidId == 0 || expectedBidVersion == 0)
		{
			outError = "Invalid bid refund request.";
			return Domain::EAuctionResultCode::InvalidRequest;
		}

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Connector::MySql::FMySqlTransaction prepareTransaction(*auctionConnection);
		if (!prepareTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!auctionRepository.PrepareBidRefund(listingId, bidId, userId, expectedBidVersion, outResult, outError))
		{
			prepareTransaction.Rollback();
			return outError.find("BID_NOT_CLAIMABLE") != std::string::npos ? Domain::EAuctionResultCode::BidNotClaimable
																		   : Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (outResult.listingId != listingId)
		{
			prepareTransaction.Rollback();
			outError = "Bid refund prepare returned a mismatched listing id.";
			return Domain::EAuctionResultCode::InternalError;
		}
		if (!prepareTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}

	Domain::EAuctionResultCode FBidRefundService::Complete(
		const Database::SBidRefundPrepareResult& prepared,
		std::string& outError)
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
		if (!auctionRepository.CompleteBidRefund(
				prepared.listingId, prepared.bidId, prepared.bidderUserId, prepared.preparedVersion, outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		return Domain::EAuctionResultCode::Success;
	}

	bool FBidRefundService::Revert(
		const Database::SBidRefundPrepareResult& prepared,
		std::string& outError)
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
		if (!Database::FAuctionRepository(*connection)
				.RevertBidRefund(prepared.listingId, prepared.bidId, prepared.bidderUserId, prepared.preparedVersion, outError))
		{
			transaction.Rollback();
			return false;
		}
		return transaction.Commit(outError);
	}
}
