#pragma once

namespace AuctionHouseServer::Service
{
	class FListingCancelService final
	{
	public:
		explicit FListingCancelService(Database::SAuctionDatabaseConfig config);
		Domain::EAuctionResultCode Execute(std::uint64_t sellerUserId,
			std::uint64_t listingId,
			std::uint64_t expectedListingVersion,
			Database::SListingCancelResult& outResult,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
