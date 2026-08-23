#pragma once

namespace AuctionHouseServer::Domain
{
	enum class EAuctionListingState : std::uint8_t
	{
		RegisterPending = 1,
		Active = 2,
		BidPending = 3,
		BuyoutPending = 4,
		CancelPending = 5,
		Settling = 6,
		Sold = 7,
		Cancelled = 8,
		Expired = 9
	};

	enum class EAuctionBidState : std::uint8_t
	{
		Pending = 1,
		Highest = 2,
		OutbidClaimable = 3,
		RefundPending = 4,
		Refunded = 5,
		Won = 6,
		Failed = 7
	};

	enum class EAuctionSaleType : std::uint8_t
	{
		Bid = 1,
		Buyout = 2
	};
}
