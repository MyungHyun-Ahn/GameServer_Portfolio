#pragma once

namespace AuctionHouseServer::Domain
{
	enum class EAuctionSearchSortType : std::uint8_t
	{
		Newest = 1,
		PriceAscending = 2,
		PriceDescending = 3,
		ExpiringSoon = 4
	};

	inline constexpr bool IsValidListingSearchSortType(
		const std::uint8_t value) noexcept
	{
		return value >= static_cast<std::uint8_t>(EAuctionSearchSortType::Newest) &&
			   value <= static_cast<std::uint8_t>(EAuctionSearchSortType::ExpiringSoon);
	}

	inline constexpr bool IsValidSaleHistorySortType(
		const std::uint8_t value) noexcept
	{
		return value >= static_cast<std::uint8_t>(EAuctionSearchSortType::Newest) &&
			   value <= static_cast<std::uint8_t>(EAuctionSearchSortType::PriceDescending);
	}
}
