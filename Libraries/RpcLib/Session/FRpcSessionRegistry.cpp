#include "RpcLibPch.h"

#include "Session/FRpcSessionRegistry.h"

namespace RpcLib::Session
{
	bool FRpcSessionRegistry::Add(
		const std::uint64_t networkSessionId,
		const std::chrono::steady_clock::time_point connectedAt)
	{
		if (networkSessionId == 0)
		{
			return false;
		}

		std::lock_guard lock(m_lock);
		return m_sessions.emplace(networkSessionId, std::make_shared<FRpcSession>(networkSessionId, connectedAt)).second;
	}

	bool FRpcSessionRegistry::MarkReady(
		const std::uint64_t networkSessionId,
		const Protocol::ERpcServerType remoteServerType,
		const Protocol::FRpcServerInstanceId remoteServerInstanceId,
		const std::uint32_t protocolVersion)
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessions.find(networkSessionId);
		if (it == m_sessions.end())
		{
			return false;
		}

		for (const auto& [registeredSessionId, registeredSession] : m_sessions)
		{
			if (registeredSessionId != networkSessionId && registeredSession->IsReady() &&
				registeredSession->GetRemoteServerType() == remoteServerType &&
				registeredSession->GetRemoteServerInstanceId() == remoteServerInstanceId)
			{
				return false;
			}
		}

		return it->second->MarkReady(remoteServerType, remoteServerInstanceId, protocolVersion);
	}

	void FRpcSessionRegistry::Remove(
		const std::uint64_t networkSessionId)
	{
		std::shared_ptr<FRpcSession> removedSession;
		{
			std::lock_guard lock(m_lock);
			const auto it = m_sessions.find(networkSessionId);
			if (it == m_sessions.end())
			{
				return;
			}

			removedSession = std::move(it->second);
			m_sessions.erase(it);
		}

		removedSession->MarkDisconnected();
	}

	std::shared_ptr<FRpcSession> FRpcSessionRegistry::Find(
		const std::uint64_t networkSessionId) const
	{
		std::lock_guard lock(m_lock);
		const auto it = m_sessions.find(networkSessionId);
		return it == m_sessions.end() ? nullptr : it->second;
	}

	std::shared_ptr<FRpcSession> FRpcSessionRegistry::SelectReady(
		const Protocol::FRpcTarget& target) const
	{
		if (target.serverType == Protocol::ERpcServerType::Unknown)
		{
			return nullptr;
		}
		if (target.rpcSessionId != 0)
		{
			const std::shared_ptr<FRpcSession> exactSession = Find(target.rpcSessionId);
			if (exactSession == nullptr || !exactSession->IsReady() || exactSession->GetRemoteServerType() != target.serverType ||
				(target.serverInstanceId != 0 && exactSession->GetRemoteServerInstanceId() != target.serverInstanceId))
			{
				return nullptr;
			}

			return exactSession;
		}

		std::vector<std::shared_ptr<FRpcSession>> candidates;
		{
			std::lock_guard lock(m_lock);
			candidates.reserve(m_sessions.size());
			for (const auto& [networkSessionId, session] : m_sessions)
			{
				(void)networkSessionId;
				if (!session->IsReady() || session->GetRemoteServerType() != target.serverType)
				{
					continue;
				}

				if (target.serverInstanceId != 0 && session->GetRemoteServerInstanceId() != target.serverInstanceId)
				{
					continue;
				}

				candidates.push_back(session);
			}
		}

		if (candidates.empty())
		{
			return nullptr;
		}

		std::sort(candidates.begin(),
			candidates.end(),
			[](const std::shared_ptr<FRpcSession>& left, const std::shared_ptr<FRpcSession>& right)
			{
				return left->GetNetworkSessionId() < right->GetNetworkSessionId();
			});

		return candidates[target.routingKey % candidates.size()];
	}

	std::size_t FRpcSessionRegistry::GetSessionCount() const
	{
		std::lock_guard lock(m_lock);
		return m_sessions.size();
	}
}
