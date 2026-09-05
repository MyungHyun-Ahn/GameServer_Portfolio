#pragma once

namespace AuctionHouseServer::Database
{
	struct SAuctionDatabaseConfig
	{
		bool enabled = false;
		Connector::MySql::SMySqlConnectionConfig auctionPrimary;
		std::vector<Connector::MySql::SMySqlConnectionConfig> auctionReplicas;
		std::uint32_t replicaReconnectCooldownMilliseconds = 60000;
	};

	struct SInventoryItem
	{
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		bool equipped = false;
		bool tradable = false;
		std::string itemDataJson;
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
		std::uint64_t version = 0;
	};

	struct SMyBid
	{
		std::uint64_t bidId = 0;
		std::uint64_t listingId = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t bidAmount = 0;
		std::uint8_t bidState = 0;
		std::uint64_t bidVersion = 0;
		std::uint32_t itemDataId = 0;
		std::uint64_t currentBidPrice = 0;
		std::uint64_t highestBidderUserId = 0;
		std::uint8_t listingState = 0;
	};

	struct SListingPrepareRequest
	{
		std::uint64_t sellerUserId = 0;
		std::string sellerLoginId;
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint8_t itemCategory = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::string searchName;
		std::uint32_t searchStr = 0;
		std::uint32_t searchDex = 0;
		std::uint32_t searchInt = 0;
		std::uint32_t searchLuk = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t startPrice = 0;
		std::uint64_t buyoutPrice = 0;
		std::uint32_t durationSeconds = 0;
		std::uint32_t maxActiveListings = 0;
	};

	struct SListingPrepareResult
	{
		std::uint64_t listingId = 0;
		std::uint64_t version = 0;
	};

	struct SListingSearchQuery
	{
		std::uint8_t itemCategory = 0;
		std::vector<std::uint32_t> itemDataIds;
		std::uint32_t minStr = 0;
		std::uint32_t minDex = 0;
		std::uint32_t minInt = 0;
		std::uint32_t minLuk = 0;
		std::uint64_t sellerUserId = 0;
		std::uint8_t sortType = 1;
		std::uint64_t cursorSortValue = 0;
		std::uint64_t cursorListingId = 0;
		std::uint32_t limit = 0;
	};

	struct SListingSummary
	{
		std::uint64_t listingId = 0;
		std::uint64_t sellerUserId = 0;
		std::string sellerLoginId;
		std::uint32_t itemDataId = 0;
		std::uint8_t itemCategory = 0;
		std::uint32_t quantity = 0;
		std::string name;
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t startPrice = 0;
		std::uint64_t currentBidPrice = 0;
		std::uint64_t buyoutPrice = 0;
		std::uint64_t expiresAtUnixMs = 0;
		std::uint64_t version = 0;
	};

	struct SListingDetail : SListingSummary
	{
		std::uint64_t itemInstanceId = 0;
		std::string itemDataJson;
		std::uint64_t highestBidderUserId = 0;
	};

	struct SSaleHistoryQuery
	{
		std::uint8_t itemCategory = 0;
		std::vector<std::uint32_t> itemDataIds;
		std::uint32_t minStr = 0;
		std::uint32_t minDex = 0;
		std::uint32_t minInt = 0;
		std::uint32_t minLuk = 0;
		std::uint8_t sortType = 1;
		std::uint64_t cursorSortValue = 0;
		std::uint64_t cursorListingId = 0;
		std::uint32_t limit = 0;
	};

	struct SSaleHistorySummary
	{
		std::uint64_t listingId = 0;
		std::uint32_t itemDataId = 0;
		std::uint8_t itemCategory = 0;
		std::uint32_t quantity = 0;
		std::string name;
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t finalPrice = 0;
		std::uint8_t saleType = 0;
		std::uint64_t soldAtUnixMs = 0;
	};

	struct SSaleHistoryDetail : SSaleHistorySummary
	{
		std::string sellerLoginId;
		std::string itemDataJson;
		std::uint64_t startPrice = 0;
	};

	struct SBidPrepareResult
	{
		std::uint64_t bidId = 0;
		std::uint64_t additionalDebit = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t previousHighestBidId = 0;
		std::uint64_t previousHighestBidderUserId = 0;
		std::uint64_t previousHighestAmount = 0;
		std::uint64_t preparedListingVersion = 0;
	};

	struct SBidResult
	{
		std::uint64_t bidId = 0;
		std::uint64_t bidAmount = 0;
		std::uint64_t additionalDebit = 0;
		std::uint64_t currencyBalance = 0;
		std::uint64_t listingVersion = 0;
		std::uint64_t previousHighestBidId = 0;
		std::uint64_t previousHighestBidderUserId = 0;
		std::uint64_t previousHighestAmount = 0;
	};

	struct SOutbidClaimable
	{
		std::uint64_t listingId = 0;
		std::uint64_t bidId = 0;
		std::uint64_t heldAmount = 0;
		std::uint64_t newHighestAmount = 0;
	};

	struct SBuyoutPrepareResult
	{
		std::uint64_t sellerUserId = 0;
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::uint16_t currencyId = 0;
		std::uint64_t buyoutPrice = 0;
		std::uint64_t additionalDebit = 0;
		std::uint64_t previousHighestBidId = 0;
		std::uint64_t previousHighestBidderUserId = 0;
		std::uint64_t previousHighestAmount = 0;
		std::uint64_t preparedListingVersion = 0;
	};

	struct SBuyoutResult
	{
		std::uint64_t buyoutPrice = 0;
		std::uint64_t additionalDebit = 0;
		std::uint64_t currencyBalance = 0;
		std::uint64_t itemMailId = 0;
		std::uint64_t sellerMailId = 0;
		std::uint64_t listingVersion = 0;
		std::uint64_t previousHighestBidId = 0;
		std::uint64_t previousHighestBidderUserId = 0;
		std::uint64_t previousHighestAmount = 0;
	};

	struct SListingCancelPrepareResult
	{
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::uint64_t preparedListingVersion = 0;
	};

	struct SListingCancelResult
	{
		std::uint64_t returnMailId = 0;
		std::uint64_t listingVersion = 0;
	};

	struct SExpirationPrepareResult
	{
		std::uint64_t sellerUserId = 0;
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		std::uint16_t currencyId = 0;
		std::uint64_t highestBidId = 0;
		std::uint64_t winnerUserId = 0;
		std::uint64_t finalPrice = 0;
		std::uint64_t preparedListingVersion = 0;
	};

	struct SExpirationResult
	{
		std::uint64_t listingId = 0;
		std::uint64_t highestBidId = 0;
		std::uint64_t winnerUserId = 0;
		std::uint64_t finalPrice = 0;
		std::uint64_t itemMailId = 0;
		std::uint64_t sellerMailId = 0;
		std::uint64_t listingVersion = 0;
	};

	struct SBidRefundPrepareResult
	{
		std::uint64_t bidId = 0;
		std::uint64_t listingId = 0;
		std::uint64_t bidderUserId = 0;
		std::uint16_t currencyId = 0;
		std::uint64_t bidAmount = 0;
		std::uint64_t preparedVersion = 0;
	};

	struct SBidRefundResult
	{
		std::uint64_t refundedAmount = 0;
		std::uint64_t currencyBalance = 0;
		std::uint8_t bidState = 0;
		std::uint64_t bidVersion = 0;
	};
}
