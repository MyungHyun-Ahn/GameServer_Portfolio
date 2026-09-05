#include "RpcLibPch.h"

#include "Session/FRpcSession.h"

namespace RpcLib::Session
{
	FRpcSession::FRpcSession(
		const std::uint64_t networkSessionId,
		const std::chrono::steady_clock::time_point connectedAt) noexcept
		: m_networkSessionId(networkSessionId)
		, m_connectedAt(connectedAt)
	{
	}

	std::uint64_t FRpcSession::GetNetworkSessionId() const noexcept
	{
		return m_networkSessionId;
	}

	std::chrono::steady_clock::time_point FRpcSession::GetConnectedAt() const noexcept
	{
		return m_connectedAt;
	}

	Protocol::ERpcSessionState FRpcSession::GetState() const noexcept
	{
		return m_state.load(std::memory_order_acquire);
	}

	Protocol::ERpcServerType FRpcSession::GetRemoteServerType() const noexcept
	{
		return m_remoteServerType;
	}

	Protocol::FRpcServerInstanceId FRpcSession::GetRemoteServerInstanceId() const noexcept
	{
		return m_remoteServerInstanceId;
	}

	std::uint32_t FRpcSession::GetProtocolVersion() const noexcept
	{
		return m_protocolVersion;
	}

	bool FRpcSession::IsReady() const noexcept
	{
		return GetState() == Protocol::ERpcSessionState::Ready;
	}

	bool FRpcSession::BeginHandshake() noexcept
	{
		Protocol::ERpcSessionState expected = Protocol::ERpcSessionState::Connected;
		return m_state.compare_exchange_strong(
			expected, Protocol::ERpcSessionState::Handshaking, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	bool FRpcSession::MarkReady(
		const Protocol::ERpcServerType remoteServerType,
		const Protocol::FRpcServerInstanceId remoteServerInstanceId,
		const std::uint32_t protocolVersion) noexcept
	{
		const Protocol::ERpcSessionState state = GetState();
		if (remoteServerType == Protocol::ERpcServerType::Unknown || remoteServerInstanceId == 0 ||
			protocolVersion != Protocol::kRpcProtocolVersion || state == Protocol::ERpcSessionState::Ready ||
			state == Protocol::ERpcSessionState::Disconnected)
		{
			return false;
		}

		m_remoteServerType = remoteServerType;
		m_remoteServerInstanceId = remoteServerInstanceId;
		m_protocolVersion = protocolVersion;
		m_state.store(Protocol::ERpcSessionState::Ready, std::memory_order_release);
		return true;
	}

	void FRpcSession::MarkDisconnected() noexcept
	{
		m_state.store(Protocol::ERpcSessionState::Disconnected, std::memory_order_release);
	}
}
