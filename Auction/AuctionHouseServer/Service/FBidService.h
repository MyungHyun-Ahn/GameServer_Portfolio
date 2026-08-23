#pragma once

namespace AuctionHouseServer::Service
{
	class FBidService final
	{
	public:
		explicit FBidService(Database::SAuctionDatabaseConfig config);

		Domain::EAuctionResultCode Execute(std::uint64_t bidderUserId,
			std::uint64_t listingId,
			std::uint64_t bidAmount,
			std::uint64_t expectedListingVersion,
			Database::SBidResult& outResult,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
