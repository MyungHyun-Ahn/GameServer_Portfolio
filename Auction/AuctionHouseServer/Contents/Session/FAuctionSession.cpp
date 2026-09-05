#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Session/FAuctionSession.h"

namespace AuctionHouseServer::Contents
{
	FAuctionSession::FAuctionSession(
		const std::uint64_t sessionId) noexcept
		: FContentSession(sessionId)
	{
	}

	void FAuctionSession::Authenticate(
		const std::uint64_t userId,
		std::string loginId)
	{
		m_userId = userId;
		m_loginId = std::move(loginId);
	}

	bool FAuctionSession::IsAuthenticated() const noexcept
	{
		return m_userId != 0;
	}

	std::uint64_t FAuctionSession::GetUserId() const noexcept
	{
		return m_userId;
	}

	const std::string& FAuctionSession::GetLoginId() const noexcept
	{
		return m_loginId;
	}
}
