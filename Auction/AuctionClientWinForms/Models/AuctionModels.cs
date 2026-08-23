namespace AuctionClientWinForms.Models;

internal sealed record DebugCheatResult(
    ushort ResultCode,
    byte CheatType,
    ulong CurrencyBalance,
    ulong ItemInstanceId,
    string Message);

internal sealed record AuctionConnectionSettings(string Host, int Port, byte PacketKey);

internal sealed record AuctionAuthResult(
	ushort ResultCode,
	ulong RequestId,
	ulong UserId,
	uint MaxActiveListings,
	uint SearchPageSize,
	uint MinimumListingDurationSeconds,
	uint MaximumListingDurationSeconds,
	uint DefaultListingDurationSeconds);

internal sealed record ListingSummary(
    ulong ListingId,
    ulong SellerUserId,
    string SellerLoginId,
    uint ItemDataId,
    byte ItemCategory,
    uint Quantity,
    string Name,
    uint Strength,
    uint Dexterity,
    uint Intelligence,
    uint Luck,
    ushort CurrencyId,
    ulong StartPrice,
    ulong CurrentBidPrice,
    ulong BuyoutPrice,
    ulong ExpiresAtUnixMs,
    ulong Version);

internal sealed record ListingDetail(
    ulong ListingId,
    ulong SellerUserId,
    string SellerLoginId,
    ulong ItemInstanceId,
    uint ItemDataId,
    byte ItemCategory,
    uint Quantity,
    string ItemData,
    string Name,
    uint Strength,
    uint Dexterity,
    uint Intelligence,
    uint Luck,
    ushort CurrencyId,
    ulong StartPrice,
    ulong CurrentBidPrice,
    ulong BuyoutPrice,
    ulong HighestBidderUserId,
    ulong ExpiresAtUnixMs,
    ulong Version);

internal sealed record BidSummary(
    ulong BidId,
    ulong ListingId,
    ushort CurrencyId,
    ulong BidAmount,
    byte BidState,
    ulong BidVersion,
    ulong CurrentBidPrice,
    byte ListingState);

internal sealed record InventoryItem(
    ulong ItemInstanceId,
    uint ItemDataId,
    uint Quantity,
    bool IsEquipped,
    bool IsTradable,
    string ItemData,
    ulong Version);

internal sealed record MailSummary(
    ulong MailId,
    byte MailType,
    string Subject,
    byte State,
    ulong ExpiresAtUnixMs,
    ulong CreatedAtUnixMs);

internal sealed record MailAttachment(
    ulong AttachmentId,
    byte AttachmentType,
    ulong ItemInstanceId,
    uint ItemDataId,
    uint Quantity,
    string ItemData,
    ushort CurrencyId,
    ulong CurrencyAmount,
    byte State);

internal sealed record MailDetail(
    ulong MailId,
    byte MailType,
    string Subject,
    string Body,
    byte State,
    ulong ExpiresAtUnixMs,
    IReadOnlyList<MailAttachment> Attachments);

internal sealed record AuctionOutbidNotification(
    ulong ListingId,
    ulong BidId,
    ulong HeldAmount,
    ulong NewHighestAmount);

internal sealed record AuctionWonNotification(
    ulong ListingId,
    ulong BidId,
    ulong FinalPrice,
    ulong ItemMailId);

internal sealed record BidResult(
    ushort ResultCode,
    ulong ListingId,
    ulong BidId,
    ulong BidAmount,
    ulong CurrencyBalance,
    ulong ListingVersion);

internal sealed record BuyoutResult(
    ushort ResultCode,
    ulong ListingId,
    ulong BuyoutPrice,
    ulong CurrencyBalance,
    ulong ItemMailId,
    ulong ListingVersion);

internal sealed record MailClaimResult(
    ushort ResultCode,
    ulong MailId,
    ulong AttachmentId,
    byte AttachmentType,
    ulong ItemInstanceId,
    uint ItemDataId,
    uint Quantity,
    ushort CurrencyId,
    ulong CurrencyAmount,
    ulong CurrencyBalance,
    byte MailState);

internal sealed record ListingRegisterResult(ushort ResultCode, ulong ListingId);

internal sealed record ListingCancelResult(
    ushort ResultCode,
    ulong ListingId,
    ulong ReturnMailId,
    ulong ListingVersion);

internal sealed record SaleHistorySummary(
    ulong ListingId,
    uint ItemDataId,
    byte ItemCategory,
    uint Quantity,
    string Name,
    uint Strength,
    uint Dexterity,
    uint Intelligence,
    uint Luck,
    ushort CurrencyId,
    ulong FinalPrice,
    byte SaleType,
    ulong SoldAtUnixMs);

internal sealed record SaleHistoryDetail(
    ulong ListingId,
    string SellerLoginId,
    uint ItemDataId,
    byte ItemCategory,
    uint Quantity,
    string ItemData,
    string Name,
    uint Strength,
    uint Dexterity,
    uint Intelligence,
    uint Luck,
    ushort CurrencyId,
    ulong StartPrice,
    ulong FinalPrice,
    byte SaleType,
    ulong SoldAtUnixMs);

internal sealed record BidRefundResult(
    ushort ResultCode,
    ulong BidId,
    ulong RefundedAmount,
    ulong CurrencyBalance,
    byte BidState,
    ulong BidVersion);
