#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Contents/Session/FAuctionSessionRegistry.h"

#include "AuctionHouseServer/Contents/Session/FAuctionSession.h"

namespace AuctionHouseServer::Contents
{
	bool FAuctionSessionRegistry::Add(
		const std::uint64_t sessionId)
	{
		if (sessionId == 0)
		{
			return false;
		}

		std::lock_guard lock(m_lock);
		return m_sessions.emplace(sessionId, std::make_shared<FAuctionSession>(sessionId)).second;
	}

	bool FAuctionSessionRegistry::Bind(
		const std::uint64_t sessionId,
		const std::uint64_t userId,
		std::string loginId,
		std::optional<std::uint64_t>& outPreviousSessionId)
	{
		outPreviousSessionId.reset();
		if (sessionId == 0 || userId == 0)
		{
			return false;
		}

		std::shared_ptr<FAuctionSession> previousSession;
		{
			std::lock_guard lock(m_lock);
			const auto sessionIt = m_sessions.find(sessionId);
			if (sessionIt == m_sessions.end())
			{
				return false;
			}

			const std::shared_ptr<FAuctionSession>& session = sessionIt->second;
			if (session->IsAuthenticated() && session->GetUserId() != userId)
			{
				const auto oldUserIt = m_userSessions.find(session->GetUserId());
				if (oldUserIt != m_userSessions.end() && oldUserIt->second == sessionId)
				{
					m_userSessions.erase(oldUserIt);
				}
			}

			const auto previousIt = m_userSessions.find(userId);
			if (previousIt != m_userSessions.end() && previousIt->second != sessionId)
			{
				outPreviousSessionId = previousIt->second;
				const auto previousSessionIt = m_sessions.find(previousIt->second);
				if (previousSessionIt != m_sessions.end())
				{
					previousSession = previousSessionIt->second;
					m_sessions.erase(previousSessionIt);
				}
			}

			session->Authenticate(userId, std::move(loginId));
			m_userSessions[userId] = sessionId;
		}

		if (previousSession != nullptr)
		{
			previousSession->MarkDisconnected();
		}

		return true;
	}

	void FAuctionSessionRegistry::Remove(
		const std::uint64_t sessionId)
	{
		std::shared_ptr<FAuctionSession> removedSession;
		{
			std::lock_guard lock(m_lock);
			const auto sessionIt = m_sessions.find(sessionId);
			if (sessionIt == m_sessions.end())
			{
				return;
			}

			removedSession = sessionIt->second;
			if (removedSession->IsAuthenticated())
			{
				const auto userIt = m_userSessions.find(removedSession->GetUserId());
				if (userIt != m_userSessions.end() && userIt->second == sessionId)
				{
					m_userSessions.erase(userIt);
				}
			}

			m_sessions.erase(sessionIt);
		}

		removedSession->MarkDisconnected();
	}

	std::shared_ptr<FAuctionSession> FAuctionSessionRegistry::Find(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessions.find(sessionId);
		return it == m_sessions.end() ? nullptr : it->second;
	}

	std::optional<std::uint64_t> FAuctionSessionRegistry::GetUserId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessions.find(sessionId);
		if (it == m_sessions.end() || !it->second->IsAuthenticated())
		{
			return std::nullopt;
		}

		return it->second->GetUserId();
	}

	std::optional<std::uint64_t> FAuctionSessionRegistry::GetSessionId(
		const std::uint64_t userId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_userSessions.find(userId);
		return it == m_userSessions.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
	}

	std::optional<std::string> FAuctionSessionRegistry::GetLoginId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessions.find(sessionId);
		if (it == m_sessions.end() || !it->second->IsAuthenticated())
		{
			return std::nullopt;
		}

		return it->second->GetLoginId();
	}
}
