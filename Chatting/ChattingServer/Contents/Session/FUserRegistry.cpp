#include "ChattingServerPch.h"

#include "ChattingServer/Contents/Session/FUserRegistry.h"

namespace ChattingServer::Contents
{
	void FUserRegistry::UpsertUser(
		const std::uint64_t sessionId,
		const std::uint32_t userId)
	{
		std::lock_guard<std::mutex> lock(m_lock);

		const auto existingSessionUserIt = m_sessionUsers.find(sessionId);
		if (existingSessionUserIt != m_sessionUsers.end())
		{
			const std::uint32_t previousUserId = existingSessionUserIt->second;
			if (previousUserId != userId)
			{
				const auto previousUserSessionIt = m_userSessions.find(previousUserId);
				if (previousUserSessionIt != m_userSessions.end() && previousUserSessionIt->second == sessionId)
				{
					m_userSessions.erase(previousUserSessionIt);
				}
			}
		}

		m_sessionUsers[sessionId] = userId;
		m_userSessions[userId] = sessionId;
	}

	void FUserRegistry::RemoveUser(
		const std::uint64_t sessionId)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto sessionUserIt = m_sessionUsers.find(sessionId);
		if (sessionUserIt == m_sessionUsers.end())
		{
			return;
		}

		const std::uint32_t userId = sessionUserIt->second;
		m_sessionUsers.erase(sessionUserIt);

		const auto userSessionIt = m_userSessions.find(userId);
		if (userSessionIt != m_userSessions.end() && userSessionIt->second == sessionId)
		{
			m_userSessions.erase(userSessionIt);
		}
	}

	std::optional<std::uint32_t> FUserRegistry::GetUserId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto it = m_sessionUsers.find(sessionId);
		if (it == m_sessionUsers.end())
		{
			return std::nullopt;
		}

		return it->second;
	}

	std::optional<std::uint64_t> FUserRegistry::GetSessionId(
		const std::uint32_t userId) const
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto it = m_userSessions.find(userId);
		if (it == m_userSessions.end())
		{
			return std::nullopt;
		}

		return it->second;
	}
}
