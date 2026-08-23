#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FListingCancelService.h"

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

	Domain::EAuctionResultCode FListingCancelService::Execute(
		const std::uint64_t sellerUserId,
		const std::uint64_t listingId,
		const std::uint64_t expectedListingVersion,
		Database::SListingCancelResult& outResult,
		std::string& outError) const
	{
		if (sellerUserId == 0 || listingId == 0 || expectedListingVersion == 0)
			return Domain::EAuctionResultCode::InvalidRequest;

		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		auto* gameConnection = context.GetGamePrimary(outError);
		if (auctionConnection == nullptr || gameConnection == nullptr)
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
			return Domain::EAuctionResultCode::DatabaseUnavailable;

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		Database::SListingCancelPrepareResult prepared;
		if (!auctionRepository.PrepareListingCancel(listingId, sellerUserId, expectedListingVersion, prepared, outError))
		{
			return MapPrepareError(outError);
		}
		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		std::uint64_t returnMailId = 0;
		if (!Database::FGameRepository(*gameConnection)
				.CreateItemReturnMail(sellerUserId,
					prepared.itemInstanceId,
					prepared.itemDataId,
					prepared.quantity,
					prepared.itemDataJson,
					returnMailId,
					outError))
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
		if (!auctionRepository.CompleteListingCancel(listingId, sellerUserId, prepared.preparedListingVersion, listingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		outResult.returnMailId = returnMailId;
		outResult.listingVersion = listingVersion;
		return Domain::EAuctionResultCode::Success;
	}
}
