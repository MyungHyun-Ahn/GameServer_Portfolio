#pragma once

namespace AuctionHouseServer::Service
{
	class FBuyoutService final
	{
	public:
		explicit FBuyoutService(Database::SAuctionDatabaseConfig config);

		Domain::EAuctionResultCode Execute(std::uint64_t buyerUserId,
			std::uint64_t listingId,
			std::uint64_t expectedListingVersion,
			Database::SBuyoutResult& outResult,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
