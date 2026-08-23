#pragma once

namespace GameData::Item
{
	class FItemDataTable;
}

namespace GameData::Auction
{
	class FAuctionPolicyTable;
}

namespace AuctionHouseServer::Service
{
	struct SListingRegistrationFailureContext
	{
		std::string failedStep;
		std::string auctionDbCommit = "NOT_ATTEMPTED";
		std::string gameDbCommit = "NOT_ATTEMPTED";
		std::string remainingListingState = "NONE";
		bool faultInjected = false;

		bool HasFailure() const noexcept
		{
			return !failedStep.empty();
		}
	};

	class FListingRegistrationService final
	{
	public:
		FListingRegistrationService(Database::SAuctionDatabaseConfig databaseConfig,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			bool faultInjectionAfterAuctionCommit);

		Domain::EAuctionResultCode Execute(std::uint64_t sellerUserId,
			std::string_view sellerLoginId,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint16_t currencyId,
			std::uint64_t startPrice,
			std::uint64_t buyoutPrice,
			std::uint32_t durationSeconds,
			std::uint64_t& outListingId,
			SListingRegistrationFailureContext& outFailure,
			std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> m_auctionPolicyTable;
		bool m_faultInjectionAfterAuctionCommit = false;
	};
}
