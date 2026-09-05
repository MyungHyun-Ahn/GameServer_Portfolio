#pragma once

namespace RpcLib::Session
{
	class FRpcSession final
	{
	public:
		FRpcSession(std::uint64_t networkSessionId, std::chrono::steady_clock::time_point connectedAt) noexcept;

		FRpcSession(const FRpcSession&) = delete;
		FRpcSession& operator=(const FRpcSession&) = delete;
		FRpcSession(FRpcSession&&) = delete;
		FRpcSession& operator=(FRpcSession&&) = delete;

		std::uint64_t GetNetworkSessionId() const noexcept;
		std::chrono::steady_clock::time_point GetConnectedAt() const noexcept;
		Protocol::ERpcSessionState GetState() const noexcept;
		Protocol::ERpcServerType GetRemoteServerType() const noexcept;
		Protocol::FRpcServerInstanceId GetRemoteServerInstanceId() const noexcept;
		std::uint32_t GetProtocolVersion() const noexcept;
		bool IsReady() const noexcept;

		bool BeginHandshake() noexcept;
		bool MarkReady(Protocol::ERpcServerType remoteServerType,
			Protocol::FRpcServerInstanceId remoteServerInstanceId,
			std::uint32_t protocolVersion) noexcept;
		void MarkDisconnected() noexcept;

	private:
		std::uint64_t m_networkSessionId = 0;
		std::chrono::steady_clock::time_point m_connectedAt;
		Protocol::ERpcServerType m_remoteServerType = Protocol::ERpcServerType::Unknown;
		Protocol::FRpcServerInstanceId m_remoteServerInstanceId = 0;
		std::uint32_t m_protocolVersion = 0;
		std::atomic<Protocol::ERpcSessionState> m_state = Protocol::ERpcSessionState::Connected;
	};
}
