#pragma once

namespace Connector::MySql
{
	class FMySqlConnection;
}

namespace AuctionHouseServer::Database
{
	class FAuctionRepository final
	{
	public:
		explicit FAuctionRepository(Connector::MySql::FMySqlConnection& connection);

		bool PrepareListing(const SListingPrepareRequest& request, SListingPrepareResult& outResult, std::string& outError);
		bool ActivateListing(std::uint64_t listingId, std::uint64_t expectedVersion, std::string& outError);
		bool DeletePendingListing(std::uint64_t listingId,
			std::uint64_t sellerUserId,
			std::uint64_t expectedVersion,
			std::string& outError);
		bool SearchListings(const SListingSearchQuery& query, std::vector<SListingSummary>& outListings, std::string& outError);
		bool GetListingDetail(std::uint64_t listingId, SListingDetail& outListing, bool& outFound, std::string& outError);
		bool SearchSaleHistory(const SSaleHistoryQuery& query, std::vector<SSaleHistorySummary>& outHistory, std::string& outError);
		bool GetSaleHistoryDetail(std::uint64_t listingId, SSaleHistoryDetail& outHistory, bool& outFound, std::string& outError);
		bool PrepareBid(std::uint64_t listingId,
			std::uint64_t bidderUserId,
			std::uint64_t bidAmount,
			std::uint64_t expectedListingVersion,
			std::uint64_t minimumBidIncrement,
			SBidPrepareResult& outResult,
			std::string& outError);
		bool CompleteBid(std::uint64_t listingId,
			std::uint64_t bidderUserId,
			std::uint64_t bidAmount,
			std::uint64_t expectedListingVersion,
			std::uint64_t& outBidId,
			std::uint64_t& outListingVersion,
			std::string& outError);
		bool RevertBid(std::uint64_t listingId, std::uint64_t expectedListingVersion, std::string& outError);
		bool GetOutbidClaimable(std::uint64_t bidderUserId, std::vector<SOutbidClaimable>& outBids, std::string& outError);
		bool PrepareBuyout(std::uint64_t listingId,
			std::uint64_t buyerUserId,
			std::uint64_t expectedListingVersion,
			SBuyoutPrepareResult& outResult,
			std::string& outError);
		bool CompleteBuyout(std::uint64_t listingId,
			std::uint64_t buyerUserId,
			std::uint64_t expectedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError);
		bool RevertBuyout(std::uint64_t listingId, std::uint64_t expectedListingVersion, std::string& outError);
		bool PrepareListingCancel(std::uint64_t listingId,
			std::uint64_t sellerUserId,
			std::uint64_t expectedListingVersion,
			SListingCancelPrepareResult& outResult,
			std::string& outError);
		bool CompleteListingCancel(std::uint64_t listingId,
			std::uint64_t sellerUserId,
			std::uint64_t expectedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError);
		bool RevertListingCancel(std::uint64_t listingId,
			std::uint64_t sellerUserId,
			std::uint64_t expectedListingVersion,
			std::string& outError);
		bool GetExpiredListingCandidates(std::uint32_t limit, std::vector<std::uint64_t>& outListingIds, std::string& outError);
		bool PrepareExpiration(std::uint64_t listingId, SExpirationPrepareResult& outResult, std::string& outError);
		bool CompleteExpiration(std::uint64_t listingId,
			std::uint64_t winnerUserId,
			std::uint64_t finalPrice,
			std::uint64_t expectedListingVersion,
			std::uint64_t& outListingVersion,
			std::string& outError);
		bool RevertExpiration(std::uint64_t listingId, std::uint64_t expectedListingVersion, std::string& outError);

		bool GetMyBids(std::uint64_t userId,
			std::uint64_t cursorBidId,
			std::uint32_t limit,
			std::vector<SMyBid>& outBids,
			std::string& outError);
		bool PrepareBidRefund(std::uint64_t listingId,
			std::uint64_t bidId,
			std::uint64_t bidderUserId,
			std::uint64_t expectedVersion,
			SBidRefundPrepareResult& outResult,
			std::string& outError);
		bool CompleteBidRefund(std::uint64_t listingId,
			std::uint64_t bidId,
			std::uint64_t bidderUserId,
			std::uint64_t expectedVersion,
			std::string& outError);
		bool RevertBidRefund(std::uint64_t listingId,
			std::uint64_t bidId,
			std::uint64_t bidderUserId,
			std::uint64_t expectedVersion,
			std::string& outError);

	private:
		Connector::MySql::FMySqlConnection& m_connection;
	};
}
