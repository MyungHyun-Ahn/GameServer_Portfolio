#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBuyoutService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"

namespace AuctionHouseServer::Service
{
	namespace
	{
		Domain::EAuctionResultCode MapPrepareError(
			const std::string& error)
		{
			if (error.find("BUYOUT_NOT_AVAILABLE") != std::string::npos)
			{
				return Domain::EAuctionResultCode::BuyoutNotAvailable;
			}
			if (error.find("SELLER_CANNOT_BUY") != std::string::npos)
			{
				return Domain::EAuctionResultCode::SellerCannotBuy;
			}
			if (error.find("LISTING_VERSION_MISMATCH") != std::string::npos)
			{
				return Domain::EAuctionResultCode::ListingVersionMismatch;
			}
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
	}

	FBuyoutService::FBuyoutService(
		Database::SAuctionDatabaseConfig config)
		: m_config(std::move(config))
	{
	}

	Domain::EAuctionResultCode FBuyoutService::Prepare(
		const std::uint64_t buyerUserId,
		const std::uint64_t listingId,
		const std::uint64_t expectedListingVersion,
		Database::SBuyoutPrepareResult& outResult,
		std::string& outError) const
	{
		if (buyerUserId == 0 || listingId == 0 || expectedListingVersion == 0)
		{
			outError = "invalid buyout request.";
			return Domain::EAuctionResultCode::InvalidRequest;
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
		if (!auctionRepository.PrepareBuyout(listingId, buyerUserId, expectedListingVersion, outResult, outError))
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

	Domain::EAuctionResultCode FBuyoutService::Complete(
		const std::uint64_t buyerUserId,
		const std::uint64_t listingId,
		const std::uint64_t preparedListingVersion,
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
		if (!auctionRepository.CompleteBuyout(listingId, buyerUserId, preparedListingVersion, outListingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		outError.clear();
		return Domain::EAuctionResultCode::Success;
	}

	bool FBuyoutService::Revert(
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
		if (!Database::FAuctionRepository(*connection).RevertBuyout(listingId, preparedListingVersion, outError))
		{
			transaction.Rollback();
			return false;
		}
		return transaction.Commit(outError);
	}
}
