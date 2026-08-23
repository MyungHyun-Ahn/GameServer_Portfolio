#pragma once

namespace AuctionHouseServer::Service
{
	class FExpirationService final
	{
	public:
		explicit FExpirationService(Database::SAuctionDatabaseConfig config);
		bool GetCandidates(std::uint32_t limit, std::vector<std::uint64_t>& outListingIds, std::string& outError) const;
		Domain::EAuctionResultCode Execute(std::uint64_t listingId, Database::SExpirationResult& outResult, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
