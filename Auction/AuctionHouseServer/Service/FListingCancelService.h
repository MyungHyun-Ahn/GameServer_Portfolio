#pragma once

namespace AuctionHouseServer::Service
{
	class FListingCancelService final
	{
	public:
		explicit FListingCancelService(Database::SAuctionDatabaseConfig config);
		Domain::EAuctionResultCode Prepare(std::uint64_t sellerUserId,
			std::uint64_t listingId,
			std::uint64_t expectedListingVersion,
			Database::SListingCancelPrepareResult& outResult,
			std::string& outError) const;
		Domain::EAuctionResultCode Complete(std::uint64_t sellerUserId,
			std::uint64_t listingId,
			std::uint64_t preparedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError) const;
		bool Revert(std::uint64_t sellerUserId, std::uint64_t listingId, std::uint64_t preparedListingVersion, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
