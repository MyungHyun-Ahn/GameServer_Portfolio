#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBidRefundService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"
#include "AuctionHouseServer/Database/FGameRepository.h"
#include "AuctionHouseServer/Domain/AuctionState.h"

namespace AuctionHouseServer::Service
{
	FBidRefundService::FBidRefundService(
		const Database::SAuctionDatabaseConfig& config)
		: m_config(config)
	{
	}

	Domain::EAuctionResultCode FBidRefundService::Execute(
		const std::uint64_t userId,
		const std::uint64_t listingId,
		const std::uint64_t bidId,
		const std::uint64_t expectedBidVersion,
		Database::SBidRefundResult& outResult,
		std::string& outError)
	{
		if (userId == 0 || listingId == 0 || bidId == 0 || expectedBidVersion == 0)
		{
			outError = "Invalid bid refund request.";
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
		Database::SBidRefundPrepareResult prepared;
		Connector::MySql::FMySqlTransaction prepareTransaction(*auctionConnection);
		if (!prepareTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!auctionRepository.PrepareBidRefund(listingId, bidId, userId, expectedBidVersion, prepared, outError) ||
			prepared.listingId != listingId)
		{
			return Domain::EAuctionResultCode::BidNotClaimable;
		}
		if (!prepareTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		std::uint64_t currencyBalance = 0;
		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!gameRepository.CreditCurrency(
				userId, prepared.currencyId, prepared.bidAmount, m_config.maxCurrencyAmount, currencyBalance, outError))
		{
			gameTransaction.Rollback();
			std::string compensationError;
			Connector::MySql::FMySqlTransaction compensationTransaction(*auctionConnection);
			if (compensationTransaction.Begin(compensationError))
			{
				if (auctionRepository.RevertBidRefund(listingId, bidId, userId, prepared.preparedVersion, compensationError))
				{
					compensationTransaction.Commit(compensationError);
				}
			}
			return Domain::EAuctionResultCode::CurrencyLimitExceeded;
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
		if (!auctionRepository.CompleteBidRefund(listingId, bidId, userId, prepared.preparedVersion, outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		if (!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outResult.refundedAmount = prepared.bidAmount;
		outResult.currencyBalance = currencyBalance;
		outResult.bidState = static_cast<std::uint8_t>(Domain::EAuctionBidState::Refunded);
		outResult.bidVersion = prepared.preparedVersion + 1;
		return Domain::EAuctionResultCode::Success;
	}
}
