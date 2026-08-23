#pragma once

namespace AuctionHouseServer::Domain
{
	enum class EAuctionResultCode : std::uint16_t
	{
		Success = 0,
		ServerBusy = 1,
		InvalidRequest = 2,
		DatabaseUnavailable = 3,
		BidNotClaimable = 4,
		CurrencyLimitExceeded = 5,
		PartialCommit = 6,
		InternalError = 7,
		AuthRequired = 8,
		AuthenticationFailed = 9,
		AlreadyAuthenticated = 10,
		InventoryItemNotFound = 11,
		ItemNotTradable = 12,
		ItemVersionMismatch = 13,
		ItemEquipped = 14,
		ListingNotFound = 15,
		BidTooLow = 16,
		InsufficientCurrency = 17,
		SellerCannotBid = 18,
		ListingVersionMismatch = 19,
		BuyoutNotAvailable = 20,
		SellerCannotBuy = 21,
		MailNotFound = 22,
		MailAttachmentNotClaimable = 23,
		InventoryFull = 24,
		ItemInstanceConflict = 25,
		NotListingOwner = 26,
		CancelNotAvailable = 27,
		HighestBidExists = 28,
		ExpireNotAvailable = 29,
		ListingLimitExceeded = 30,
		BidStateInvalid = 31
	};
}
