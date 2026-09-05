#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Service/FExpirationService.h"

#include "AuctionHouseServer/Database/FAuctionRepository.h"
#include "AuctionHouseServer/Database/FContentThreadDbContext.h"

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

	Domain::EAuctionResultCode FExpirationService::Prepare(
		const std::uint64_t listingId,
		Database::SExpirationPrepareResult& outResult,
		std::string& outError) const
	{
		auto& context = Database::FContentThreadDbContext::Get(m_config);
		auto* auctionConnection = context.GetAuctionPrimary(outError);
		if (auctionConnection == nullptr)
			return Domain::EAuctionResultCode::DatabaseUnavailable;
		Connector::MySql::FMySqlTransaction auctionTransaction(*auctionConnection);
		if (!auctionTransaction.Begin(outError))
			return Domain::EAuctionResultCode::DatabaseUnavailable;

		Database::FAuctionRepository auctionRepository(*auctionConnection);
		if (!auctionRepository.PrepareExpiration(listingId, outResult, outError))
		{
			auctionTransaction.Rollback();
			return outError.find("EXPIRE_NOT_AVAILABLE") != std::string::npos ? Domain::EAuctionResultCode::ExpireNotAvailable
																			  : Domain::EAuctionResultCode::DatabaseUnavailable;
		}
		if (!auctionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}

	Domain::EAuctionResultCode FExpirationService::Complete(
		const std::uint64_t listingId,
		const std::uint64_t winnerUserId,
		const std::uint64_t finalPrice,
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
		if (!auctionRepository.CompleteExpiration(
				listingId, winnerUserId, finalPrice, preparedListingVersion, outListingVersion, outError) ||
			!completionTransaction.Commit(outError))
		{
			return Domain::EAuctionResultCode::PartialCommit;
		}
		return Domain::EAuctionResultCode::Success;
	}

	bool FExpirationService::Revert(
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
		if (!Database::FAuctionRepository(*connection).RevertExpiration(listingId, preparedListingVersion, outError))
		{
			transaction.Rollback();
			return false;
		}
		return transaction.Commit(outError);
	}
}
