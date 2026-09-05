#include "RpcLibPch.h"

#include "Call/FRpcPendingCallManager.h"

namespace RpcLib::Call
{
	namespace
	{
		void NotifyFailure(
			FRpcPendingCall& pendingCall,
			const Protocol::ERpcCallError error,
			const Protocol::ERpcResponseCode remoteResponseCode = Protocol::ERpcResponseCode::Success)
		{
			if (pendingCall.onFailure)
			{
				try
				{
					pendingCall.onFailure(Protocol::FRpcCallFailure{error, remoteResponseCode});
				}
				catch (...)
				{
					// User callbacks must not unwind through the content execution loop.
				}
			}
		}
	}

	FRpcPendingCallManager::FRpcPendingCallManager(
		const std::size_t maxPendingCallCount) noexcept
		: m_maxPendingCallCount(maxPendingCallCount)
	{
	}

	bool FRpcPendingCallManager::TryAdd(
		FRpcPendingCall pendingCall)
	{
		if (pendingCall.requestId == 0 || pendingCall.rpcSessionId == 0 || pendingCall.serviceId == 0 || pendingCall.methodId == 0 ||
			!pendingCall.onResponse || m_pendingCalls.size() >= m_maxPendingCallCount)
		{
			return false;
		}

		return m_pendingCalls.emplace(pendingCall.requestId, std::move(pendingCall)).second;
	}

	bool FRpcPendingCallManager::Cancel(
		const Protocol::FRpcRequestId requestId)
	{
		return m_pendingCalls.erase(requestId) != 0;
	}

	Protocol::ERpcCompletionResult FRpcPendingCallManager::Complete(
		const std::uint64_t rpcSessionId,
		const Protocol::FRpcResponse& response)
	{
		const auto it = m_pendingCalls.find(response.requestId);
		if (it == m_pendingCalls.end())
		{
			return Protocol::ERpcCompletionResult::NotFound;
		}
		if (it->second.rpcSessionId != rpcSessionId)
		{
			return Protocol::ERpcCompletionResult::SessionMismatch;
		}

		FRpcPendingCall pendingCall = std::move(it->second);
		m_pendingCalls.erase(it);

		if (response.protocolVersion != Protocol::kRpcProtocolVersion || response.serviceId != pendingCall.serviceId ||
			response.methodId != pendingCall.methodId)
		{
			NotifyFailure(pendingCall, Protocol::ERpcCallError::ProtocolError);
			return Protocol::ERpcCompletionResult::ProtocolError;
		}

		if (response.resultCode != Protocol::ERpcResponseCode::Success)
		{
			NotifyFailure(pendingCall, Protocol::ERpcCallError::RemoteError, response.resultCode);
			return Protocol::ERpcCompletionResult::RemoteError;
		}

		bool responseHandled = false;
		try
		{
			responseHandled = pendingCall.onResponse(response.payload);
		}
		catch (...)
		{
			NotifyFailure(pendingCall, Protocol::ERpcCallError::ProtocolError);
			return Protocol::ERpcCompletionResult::ProtocolError;
		}

		if (!responseHandled)
		{
			NotifyFailure(pendingCall, Protocol::ERpcCallError::ProtocolError);
			return Protocol::ERpcCompletionResult::ProtocolError;
		}

		return Protocol::ERpcCompletionResult::Completed;
	}

	std::size_t FRpcPendingCallManager::Expire(
		const std::chrono::steady_clock::time_point now)
	{
		std::vector<FRpcPendingCall> expiredCalls;
		for (auto it = m_pendingCalls.begin(); it != m_pendingCalls.end();)
		{
			if (it->second.deadline > now)
			{
				++it;
				continue;
			}

			expiredCalls.push_back(std::move(it->second));
			it = m_pendingCalls.erase(it);
		}

		for (FRpcPendingCall& pendingCall : expiredCalls)
		{
			NotifyFailure(pendingCall, Protocol::ERpcCallError::Timeout);
		}

		return expiredCalls.size();
	}

	std::size_t FRpcPendingCallManager::FailSession(
		const std::uint64_t rpcSessionId,
		const Protocol::ERpcCallError error)
	{
		std::vector<FRpcPendingCall> failedCalls;
		for (auto it = m_pendingCalls.begin(); it != m_pendingCalls.end();)
		{
			if (it->second.rpcSessionId != rpcSessionId)
			{
				++it;
				continue;
			}

			failedCalls.push_back(std::move(it->second));
			it = m_pendingCalls.erase(it);
		}

		for (FRpcPendingCall& pendingCall : failedCalls)
		{
			NotifyFailure(pendingCall, error);
		}

		return failedCalls.size();
	}

	std::size_t FRpcPendingCallManager::GetPendingCallCount() const noexcept
	{
		return m_pendingCalls.size();
	}

	std::size_t FRpcPendingCallManager::GetMaxPendingCallCount() const noexcept
	{
		return m_maxPendingCallCount;
	}
}
