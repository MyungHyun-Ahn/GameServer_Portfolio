#pragma once

namespace RpcLib::Call
{
	using FRpcResponseCallback = std::function<bool(std::span<const char>)>;
	using FRpcFailureCallback = std::function<void(const Protocol::FRpcCallFailure&)>;

	struct FRpcPendingCall final
	{
		Protocol::FRpcRequestId requestId = 0;
		std::uint64_t rpcSessionId = 0;
		Protocol::FRpcServiceId serviceId = 0;
		Protocol::FRpcMethodId methodId = 0;
		std::chrono::steady_clock::time_point deadline{};
		FRpcResponseCallback onResponse;
		FRpcFailureCallback onFailure;
	};

	// One manager belongs to one Content Instance and is accessed only by that Content Thread.
	class FRpcPendingCallManager final
	{
	public:
		explicit FRpcPendingCallManager(std::size_t maxPendingCallCount = 1024) noexcept;

		bool TryAdd(FRpcPendingCall pendingCall);
		bool Cancel(Protocol::FRpcRequestId requestId);
		Protocol::ERpcCompletionResult Complete(std::uint64_t rpcSessionId, const Protocol::FRpcResponse& response);
		std::size_t Expire(std::chrono::steady_clock::time_point now);
		std::size_t FailSession(std::uint64_t rpcSessionId, Protocol::ERpcCallError error);

		std::size_t GetPendingCallCount() const noexcept;
		std::size_t GetMaxPendingCallCount() const noexcept;

	private:
		std::size_t m_maxPendingCallCount = 0;
		std::unordered_map<Protocol::FRpcRequestId, FRpcPendingCall> m_pendingCalls;
	};
}
