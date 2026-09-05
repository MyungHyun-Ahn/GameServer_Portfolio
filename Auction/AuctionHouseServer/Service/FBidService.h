#pragma once

namespace AuctionHouseServer::Service
{
	class FBidService final
	{
	public:
		FBidService(Database::SAuctionDatabaseConfig config, std::uint64_t minimumBidIncrement);

		Domain::EAuctionResultCode Prepare(std::uint64_t bidderUserId,
			std::uint64_t listingId,
			std::uint64_t bidAmount,
			std::uint64_t expectedListingVersion,
			Database::SBidPrepareResult& outResult,
			std::string& outError) const;
		Domain::EAuctionResultCode Complete(std::uint64_t bidderUserId,
			std::uint64_t listingId,
			std::uint64_t bidAmount,
			std::uint64_t preparedListingVersion,
			std::uint64_t& outBidId,
			std::uint64_t& outListingVersion,
			std::string& outError) const;
		bool Revert(std::uint64_t listingId, std::uint64_t preparedListingVersion, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
		std::uint64_t m_minimumBidIncrement = 0;
	};
}
