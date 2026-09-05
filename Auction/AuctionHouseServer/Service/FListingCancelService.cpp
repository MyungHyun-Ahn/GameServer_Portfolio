#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FListingCancelService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"

namespace AuctionHouseServer::Service
{
	namespace
	{
		Domain::EAuctionResultCode MapPrepareError(
			const std::string& error)
		{
			if (error.find("LISTING_NOT_FOUND") != std::string::npos)
				return Domain::EAuctionResultCode::ListingNotFound;
			if (error.find("NOT_LISTING_OWNER") != std::string::npos)
				return Domain::EAuctionResultCode::NotListingOwner;
			if (error.find("LISTING_VERSION_MISMATCH") != std::string::npos)
				return Domain::EAuctionResultCode::ListingVersionMismatch;
			if (error.find("HIGHEST_BID_EXISTS") != std::string::npos)
				return Domain::EAuctionResultCode::HighestBidExists;
			if (error.find("CANCEL_NOT_AVAILABLE") != std::string::npos)
				return Domain::EAuctionResultCode::CancelNotAvailable;
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
	}

	FListingCancelService::FListingCancelService(
		Database::SAuctionDatabaseConfig config)
		: m_config(std::move(config))
	{
	}

	Domain::EAuctionResultCode FListingCancelService::Prepare(
		const std::uint64_t sellerUserId,
		const std::uint64_t listingId,
		const std::uint64_t expectedListingVersion,
		Database::SListingCancelPrepareResult& outResult,
		std::string& outError) const
	{
		if (sellerUserId == 0 || listingId == 0 || expectedListingVersion == 0)
			return Domain::EAuctionResultCode::InvalidRequest;

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
			return Domain::EAuctionResultCode::DatabaseUnavailable;

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		if (!auctionRepository.PrepareListingCancel(listingId, sellerUserId, expectedListingVersion, outResult, outError))
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

	Domain::EAuctionResultCode FListingCancelService::Complete(
		const std::uint64_t sellerUserId,
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
			return Domain::EAuctionResultCode::PartialCommit;
		if (!auctionRepository.CompleteListingCancel(listingId, sellerUserId, preparedListingVersion, outListingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}

	bool FListingCancelService::Revert(
		const std::uint64_t sellerUserId,
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
		if (!Database::FAuctionRepository(*connection).RevertListingCancel(listingId, sellerUserId, preparedListingVersion, outError))
		{
			transaction.Rollback();
			return false;
		}
		return transaction.Commit(outError);
	}
}
