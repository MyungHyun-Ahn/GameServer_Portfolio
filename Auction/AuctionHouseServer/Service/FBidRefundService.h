#pragma once

namespace AuctionHouseServer::Service
{
	class FBidRefundService final
	{
	public:
		explicit FBidRefundService(const Database::SAuctionDatabaseConfig& config);

		Domain::EAuctionResultCode Prepare(std::uint64_t userId,
			std::uint64_t listingId,
			std::uint64_t bidId,
			std::uint64_t expectedBidVersion,
			Database::SBidRefundPrepareResult& outResult,
			std::string& outError);
		Domain::EAuctionResultCode Complete(const Database::SBidRefundPrepareResult& prepared, std::string& outError);
		bool Revert(const Database::SBidRefundPrepareResult& prepared, std::string& outError);

	private:
		const Database::SAuctionDatabaseConfig& m_config;
	};
}
