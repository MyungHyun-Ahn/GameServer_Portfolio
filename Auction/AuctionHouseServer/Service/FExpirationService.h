#pragma once

namespace AuctionHouseServer::Service
{
	class FExpirationService final
	{
	public:
		explicit FExpirationService(Database::SAuctionDatabaseConfig config);
		bool GetCandidates(std::uint32_t limit, std::vector<std::uint64_t>& outListingIds, std::string& outError) const;
		Domain::EAuctionResultCode Prepare(std::uint64_t listingId,
			Database::SExpirationPrepareResult& outResult,
			std::string& outError) const;
		Domain::EAuctionResultCode Complete(std::uint64_t listingId,
			std::uint64_t winnerUserId,
			std::uint64_t finalPrice,
			std::uint64_t preparedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError) const;
		bool Revert(std::uint64_t listingId, std::uint64_t preparedListingVersion, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
