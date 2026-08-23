#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FBuyoutService.h"

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

	Domain::EAuctionResultCode FBuyoutService::Execute(
		const std::uint64_t buyerUserId,
		const std::uint64_t listingId,
		const std::uint64_t expectedListingVersion,
		Database::SBuyoutResult& outResult,
		std::string& outError) const
	{
		if (buyerUserId == 0 || listingId == 0 || expectedListingVersion == 0)
		{
			outError = "invalid buyout request.";
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
		Database::SBuyoutPrepareResult prepared;
		if (!auctionRepository.PrepareBuyout(listingId, buyerUserId, expectedListingVersion, prepared, outError))
		{
			return MapPrepareError(outError);
		}

		Connector::MySql::FMySqlTransaction gameTransaction(*gameConnection);
		if (!gameTransaction.Begin(outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		std::uint64_t currencyBalance = 0;
		if (prepared.additionalDebit > 0 &&
			!gameRepository.DebitCurrency(buyerUserId, prepared.currencyId, prepared.additionalDebit, currencyBalance, outError))
		{
			return outError.find("INSUFFICIENT_CURRENCY") != std::string::npos ? Domain::EAuctionResultCode::InsufficientCurrency
																			   : Domain::EAuctionResultCode::DatabaseUnavailable;
		}

		std::uint64_t itemMailId = 0;
		std::uint64_t sellerMailId = 0;
		if (!gameRepository.CreateItemMail(buyerUserId,
				prepared.itemInstanceId,
				prepared.itemDataId,
				prepared.quantity,
				prepared.itemDataJson,
				itemMailId,
				outError) ||
			!gameRepository.CreateCurrencyMail(prepared.sellerUserId, prepared.currencyId, prepared.buyoutPrice, sellerMailId, outError))
		{
			return Domain::EAuctionResultCode::DatabaseUnavailable;
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
		if (!auctionRepository.CompleteBuyout(listingId, buyerUserId, prepared.preparedListingVersion, listingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}

		outResult.buyoutPrice = prepared.buyoutPrice;
		outResult.additionalDebit = prepared.additionalDebit;
		outResult.currencyBalance = currencyBalance;
		outResult.itemMailId = itemMailId;
		outResult.sellerMailId = sellerMailId;
		outResult.listingVersion = listingVersion;
		outResult.previousHighestBidId = prepared.previousHighestBidId;
		outResult.previousHighestBidderUserId = prepared.previousHighestBidderUserId;
		outResult.previousHighestAmount = prepared.previousHighestAmount;
		outError.clear();
		return Domain::EAuctionResultCode::Success;
	}
}
