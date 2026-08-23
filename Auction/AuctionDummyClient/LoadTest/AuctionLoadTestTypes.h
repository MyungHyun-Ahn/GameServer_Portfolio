#pragma once

namespace AuctionDummyClient::LoadTest
{
	enum class EVirtualUserState : std::uint8_t
	{
		Disconnected,
		Authenticating,
		Ready,
		SearchPending,
		MyListingsPending,
		MyBidsPending,
		InventoryPending,
		CheatGoldPending,
		CheatItemPending,
		ListingRegisterPending,
		ListingDetailPending,
		BidPending,
		BidRefundPending,
		MailDetailPending,
		MailClaimPending,
		Failed
	};

	enum class ELoadTestOperation : std::uint8_t
	{
		AuctionAuth,
		ListingSearch,
		MyListings,
		MyBids,
		InventoryList,
		CheatGold,
		CheatItem,
		ListingRegister,
		ListingDetail,
		Bid,
		BidRefund,
		MailDetail,
		MailClaim
	};

	struct SListingCandidate final
	{
		std::uint64_t listingId = 0;
		std::uint64_t sellerUserId = 0;
		std::uint64_t startPrice = 0;
		std::uint64_t currentBidPrice = 0;
		std::uint64_t buyoutPrice = 0;
		std::uint64_t version = 0;
	};

	struct SBidRefundCandidate final
	{
		std::uint64_t listingId = 0;
		std::uint64_t bidId = 0;
		std::uint64_t version = 0;
	};

	struct SMailAttachmentCandidate final
	{
		std::uint64_t mailId = 0;
		std::uint64_t attachmentId = 0;
	};

	struct SInventoryItem final
	{
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::uint64_t version = 0;
	};

	struct SAuctionLoadTestConfig final
	{
		std::string serverIp;
		std::uint16_t port = 0;
		std::uint8_t packetKey = 0;
		std::uint32_t workerThreadCount = 0;
		std::size_t virtualUserCount = 0;
		std::uint32_t connectsPerSecond = 0;
		std::chrono::seconds runDuration{};
		std::chrono::milliseconds searchIntervalMin{};
		std::chrono::milliseconds searchIntervalMax{};
		std::chrono::milliseconds responseTimeout{};
		std::chrono::seconds consoleSummaryInterval{};
		std::size_t eventPollMaxCount = 0;
		std::uint32_t randomStatMaximum = 0;
		std::uint32_t searchWeight = 0;
		std::uint32_t registerWeight = 0;
		std::uint32_t bidWeight = 0;
		std::uint64_t initialGoldAmount = 0;
		std::uint32_t bidIncrementMinimum = 0;
		std::uint32_t bidIncrementMaximum = 0;
		std::uint32_t bidHotspotPercent = 0;
		std::uint32_t outbidRefundPercent = 0;
		std::uint32_t inventoryListLimit = 0;
		std::vector<std::uint32_t> cheatItemDataIds;
		std::uint32_t listingStartPriceMinimum = 0;
		std::uint32_t listingStartPriceMaximum = 0;
		std::uint32_t listingBuyoutMarkupMinimum = 0;
		std::uint32_t listingBuyoutMarkupMaximum = 0;
		std::uint32_t randomSeed = 0;
		std::filesystem::path ticketFilePath;
	};

	struct SPendingRequest final
	{
		std::uint64_t requestId = 0;
		std::uint16_t responseOpcode = 0;
		ELoadTestOperation operation = ELoadTestOperation::AuctionAuth;
		std::chrono::steady_clock::time_point sentAt{};
		std::chrono::steady_clock::time_point deadline{};
	};
}
