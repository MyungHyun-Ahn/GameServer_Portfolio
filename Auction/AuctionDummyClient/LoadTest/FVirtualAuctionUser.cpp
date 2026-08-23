#include "AuctionDummyClientPch.h"

#include "LoadTest/FVirtualAuctionUser.h"

namespace AuctionDummyClient::LoadTest
{
	FVirtualAuctionUser::FVirtualAuctionUser(
		std::string inTicket)
		: ticket(std::move(inTicket))
	{
	}

	std::uint64_t FVirtualAuctionUser::IssueRequestId() noexcept
	{
		return m_nextRequestId++;
	}

	bool FVirtualAuctionUser::HasPendingRequest() const noexcept
	{
		return pendingRequest.has_value();
	}
}
