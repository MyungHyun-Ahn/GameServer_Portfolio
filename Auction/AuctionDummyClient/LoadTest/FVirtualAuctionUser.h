#pragma once

namespace AuctionDummyClient::LoadTest
{
	class FVirtualAuctionUser final
	{
	public:
		explicit FVirtualAuctionUser(std::string ticket);

		std::uint64_t IssueRequestId() noexcept;
		bool HasPendingRequest() const noexcept;

	public:
		std::string ticket;
		ClientNetworkLib::FClientSessionId sessionId = 0;
		std::uint64_t authenticatedUserId = 0;
		std::uint32_t maxActiveListings = 0;
		std::uint32_t searchPageSize = 0;
		std::uint32_t defaultListingDurationSeconds = 0;
		std::uint32_t activeListingCount = 0;
		std::uint64_t currencyBalance = 0;
		EVirtualUserState state = EVirtualUserState::Disconnected;
		std::optional<SPendingRequest> pendingRequest;
		std::chrono::steady_clock::time_point nextActionAt{};
		std::vector<SInventoryItem> inventoryItems;
		std::vector<SListingCandidate> listingCandidates;
		std::unordered_set<std::uint64_t> highestBidListingIds;
		std::unordered_set<std::uint64_t> refundedBidIds;
		std::deque<std::uint64_t> outbidListingIds;
		std::deque<SBidRefundCandidate> bidRefundCandidates;
		std::deque<std::uint64_t> wonMailIds;
		std::deque<SMailAttachmentCandidate> mailAttachmentCandidates;
		std::optional<SListingCandidate> pendingBidCandidate;
		std::optional<SBidRefundCandidate> pendingBidRefundCandidate;
		std::uint64_t pendingMailId = 0;
		std::uint8_t mailDetailNotFoundRetryCount = 0;
		SMailAttachmentCandidate pendingMailAttachment;
		std::uint64_t pendingRegistrationItemInstanceId = 0;
		bool needsInitialGoldCheat = false;
		bool needsMyListingsRefresh = false;
		bool needsInventoryRefresh = false;
		bool needsMyBidsRefresh = false;

	private:
		std::uint64_t m_nextRequestId = 1;
	};
}
