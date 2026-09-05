#pragma once

namespace RpcLib::Session
{
	class FRpcSessionRegistry final
	{
	public:
		bool Add(std::uint64_t networkSessionId, std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now());
		bool MarkReady(std::uint64_t networkSessionId,
			Protocol::ERpcServerType remoteServerType,
			Protocol::FRpcServerInstanceId remoteServerInstanceId,
			std::uint32_t protocolVersion);
		void Remove(std::uint64_t networkSessionId);

		std::shared_ptr<FRpcSession> Find(std::uint64_t networkSessionId) const;
		std::shared_ptr<FRpcSession> SelectReady(const Protocol::FRpcTarget& target) const;
		std::size_t GetSessionCount() const;

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint64_t, std::shared_ptr<FRpcSession>> m_sessions;
	};
}
