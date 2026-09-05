#include "WorldServerPch.h"

#include "WorldServer/Contents/Session/FWorldSessionRegistry.h"

#include "WorldServer/Contents/Session/FWorldSession.h"

namespace WorldServer::Contents
{
	bool FWorldSessionRegistry::Add(
		const std::uint64_t sessionId)
	{
		if (sessionId == 0)
		{
			return false;
		}

		std::lock_guard lock(m_lock);
		return m_sessions.emplace(sessionId, std::make_shared<FWorldSession>(sessionId)).second;
	}

	void FWorldSessionRegistry::MarkDisconnected(
		const std::uint64_t sessionId)
	{
		const std::shared_ptr<FWorldSession> session = Find(sessionId);
		if (session != nullptr)
		{
			session->MarkDisconnected();
		}
	}

	void FWorldSessionRegistry::Remove(
		const std::uint64_t sessionId)
	{
		std::shared_ptr<FWorldSession> removedSession;
		{
			std::lock_guard lock(m_lock);
			const auto found = m_sessions.find(sessionId);
			if (found == m_sessions.end())
			{
				return;
			}

			removedSession = std::move(found->second);
			m_sessions.erase(found);
		}

		removedSession->MarkDisconnected();
	}

	std::shared_ptr<FWorldSession> FWorldSessionRegistry::Find(
		const std::uint64_t sessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto found = m_sessions.find(sessionId);
		return found == m_sessions.end() ? nullptr : found->second;
	}
}
