#pragma once

// Generated from RPC YAML. Keep deterministic layout; do not format by hand.
// clang-format off
namespace Generated::Rpc
{
	struct FRpcMethodCatalogEntry final
	{
		std::uint32_t serviceId = 0;
		std::uint32_t methodId = 0;
		const char* name = nullptr;
		const char* routingKey = nullptr;
		bool hasRequestResponse = false;
		bool hasNotification = false;
	};

	inline constexpr FRpcMethodCatalogEntry kRpcMethodCatalog[] =
	{
		{1, 1, "Cache.CachePing", "userId", true, true},
		{1, 2, "Cache.LoadCacheUser", "userId", true, false},
		{1, 3, "Cache.GetInventory", "userId", true, false},
		{1, 4, "Cache.GetMailList", "userId", true, false},
		{1, 5, "Cache.GetMailDetail", "userId", true, false},
		{1, 6, "Cache.GetCurrency", "userId", true, false},
		{1, 7, "Cache.GetInventoryItem", "userId", true, false},
		{1, 8, "Cache.CreditCurrency", "userId", true, false},
		{1, 9, "Cache.GrantInventoryItem", "userId", true, false},
		{1, 10, "Cache.ClaimMailAttachment", "userId", true, false},
		{1, 11, "Cache.ConsumeInventoryItemForListing", "userId", true, false},
		{1, 12, "Cache.DebitCurrency", "userId", true, false},
		{1, 13, "Cache.SettleBuyout", "buyerUserId", true, false},
		{1, 14, "Cache.CreateListingReturnMail", "sellerUserId", true, false},
		{1, 15, "Cache.SettleExpiration", "primaryUserId", true, false},
		{1, 16, "Cache.GetPlayerWorldSnapshot", "userId", true, false},
		{1, 17, "Cache.AllocatePlayerStat", "userId", true, false},
		{1, 18, "Cache.GrantPlayerExperience", "userId", true, false},
		{1, 19, "Cache.EquipPlayerItem", "userId", true, false},
		{1, 20, "Cache.UnequipPlayerItem", "userId", true, false},
		{1, 21, "Cache.EquipPlayerItemV2", "userId", true, false},
		{1, 22, "Cache.UnequipPlayerItemV2", "userId", true, false},
		{100, 1, "UserPresence.EnterUser", "userId", true, false},
		{100, 2, "UserPresence.LeaveUser", "userId", true, false},
		{100, 3, "UserPresence.RenewUser", "userId", true, false},
		{100, 4, "UserPresence.RevokeUser", "userId", true, false},
	};
}
// clang-format on
