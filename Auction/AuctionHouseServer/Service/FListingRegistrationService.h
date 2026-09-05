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
		std::string cacheRpc = "NOT_ATTEMPTED";
		std::string activationCommit = "NOT_ATTEMPTED";
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
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable);

		Domain::EAuctionResultCode Prepare(std::uint64_t sellerUserId,
			std::string_view sellerLoginId,
			const Database::SInventoryItem& inventoryItem,
			std::uint64_t expectedItemVersion,
			std::uint16_t currencyId,
			std::uint64_t startPrice,
			std::uint64_t buyoutPrice,
			std::uint32_t durationSeconds,
			Database::SListingPrepareResult& outResult,
			SListingRegistrationFailureContext& outFailure,
			std::string& outError) const;

		Domain::EAuctionResultCode Activate(std::uint64_t listingId,
			std::uint64_t expectedVersion,
			SListingRegistrationFailureContext& outFailure,
			std::string& outError) const;

		bool DeletePending(std::uint64_t sellerUserId, std::uint64_t listingId, std::uint64_t expectedVersion, std::string& outError) const;

	private:
		Database::SAuctionDatabaseConfig m_databaseConfig;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> m_auctionPolicyTable;
	};
}
