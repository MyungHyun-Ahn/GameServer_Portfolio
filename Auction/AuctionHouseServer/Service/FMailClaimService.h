#pragma once

namespace AuctionHouseServer::Service
{
	class FMailClaimService final
	{
	public:
		explicit FMailClaimService(Database::SAuctionDatabaseConfig config);
		Domain::EAuctionResultCode Execute(std::uint64_t userId,
			std::uint64_t mailId,
			std::uint64_t attachmentId,
			Database::SMailClaimResult& outResult,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_config;
	};
}
