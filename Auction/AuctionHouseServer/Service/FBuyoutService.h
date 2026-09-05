#pragma once

namespace AuctionHouseServer::Service
{
	class FBuyoutService final
	{
	public:
		explicit FBuyoutService(Database::SAuctionDatabaseConfig config);

		Domain::EAuctionResultCode Prepare(std::uint64_t buyerUserId,
			std::uint64_t listingId,
			std::uint64_t expectedListingVersion,
			Database::SBuyoutPrepareResult& outResult,
			std::string& outError) const;
		Domain::EAuctionResultCode Complete(std::uint64_t buyerUserId,
			std::uint64_t listingId,
			std::uint64_t preparedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError) const;
		bool Revert(std::uint64_t listingId, std::uint64_t preparedListingVersion, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
