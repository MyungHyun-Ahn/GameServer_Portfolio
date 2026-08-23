#pragma once

namespace AuctionHouseServer::Service
{
	class FBidRefundService final
	{
	public:
		explicit FBidRefundService(const Database::SAuctionDatabaseConfig& config);

		Domain::EAuctionResultCode Execute(std::uint64_t userId,
			std::uint64_t listingId,
			std::uint64_t bidId,
			std::uint64_t expectedBidVersion,
			Database::SBidRefundResult& outResult,
			std::string& outError);

	private:
		const Database::SAuctionDatabaseConfig& m_config;
	};
}
