#pragma once

namespace GameData::Auction
{
	struct SAuctionPolicy
	{
		std::uint32_t maxActiveListings = 0;
		std::uint32_t searchPageSize = 0;
		std::uint32_t minimumListingDurationSeconds = 0;
		std::uint32_t maximumListingDurationSeconds = 0;
		std::uint32_t defaultListingDurationSeconds = 0;
	};
}
