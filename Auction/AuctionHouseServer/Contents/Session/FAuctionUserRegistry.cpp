#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"

namespace AuctionHouseServer::Contents
{
	std::optional<std::uint64_t> FAuctionUserRegistry::Bind(
		const std::uint64_t sessionId,
		const std::uint64_t userId,
		std::string loginId)
	{
		std::lock_guard lock(m_lock);
		std::optional<std::uint64_t> previousSessionId;
		const auto previousIt = m_userSessions.find(userId);
		if (previousIt != m_userSessions.end() && previousIt->second != sessionId)
		{
			previousSessionId = previousIt->second;
			m_sessionUsers.erase(previousIt->second);
			m_sessionLoginIds.erase(previousIt->second);
		}

		const auto sessionIt = m_sessionUsers.find(sessionId);
		if (sessionIt != m_sessionUsers.end() && sessionIt->second != userId)
		{
			m_userSessions.erase(sessionIt->second);
		}

		m_sessionUsers[sessionId] = userId;
		m_userSessions[userId] = sessionId;
		m_sessionLoginIds[sessionId] = std::move(loginId);
		return previousSessionId;
	}

	void FAuctionUserRegistry::Remove(
		const std::uint64_t sessionId)
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessionUsers.find(sessionId);
		if (it == m_sessionUsers.end())
		{
			return;
		}
		const std::uint64_t userId = it->second;
		m_sessionUsers.erase(it);
		m_sessionLoginIds.erase(sessionId);
		const auto reverseIt = m_userSessions.find(userId);
		if (reverseIt != m_userSessions.end() && reverseIt->second == sessionId)
		{
			m_userSessions.erase(reverseIt);
		}
	}

	std::optional<std::uint64_t> FAuctionUserRegistry::GetUserId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessionUsers.find(sessionId);
		return it == m_sessionUsers.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
	}

	std::optional<std::uint64_t> FAuctionUserRegistry::GetSessionId(
		const std::uint64_t userId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_userSessions.find(userId);
		return it == m_userSessions.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
	}

	std::optional<std::string> FAuctionUserRegistry::GetLoginId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessionLoginIds.find(sessionId);
		return it == m_sessionLoginIds.end() ? std::nullopt : std::optional<std::string>(it->second);
	}
}
