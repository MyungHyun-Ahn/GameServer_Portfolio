#pragma once

namespace RpcLib::Transport
{
	class FClientRpcTransport;
}

namespace RpcLib::Client
{
	struct FOutboundRpcClientConfig final
	{
		std::string host = "127.0.0.1";
		std::uint16_t port = 0;
		Protocol::ERpcServerType localServerType = Protocol::ERpcServerType::Unknown;
		Protocol::FRpcServerInstanceId localServerInstanceId = 0;
		Protocol::ERpcServerType expectedRemoteServerType = Protocol::ERpcServerType::Unknown;
		Protocol::FRpcServerInstanceId expectedRemoteServerInstanceId = 0;
		std::uint8_t packetKey = 0;
		std::uint8_t randomKey = 0;
		std::chrono::milliseconds handshakeTimeout{5000};
		std::chrono::milliseconds reconnectInterval{1000};
		std::uint32_t networkWorkerThreadCount = 2;
		std::size_t recvScratchBufferSize = 64 * 1024;
	};

	class FOutboundRpcClient final
	{
	public:
		// Callbacks run serially on the outbound event thread. RPC message data is valid
		// only for the duration of the callback and must be copied when enqueued to a
		// Content mailbox. Stop() from another thread waits for an in-progress
		// callback. A callback may request Stop(), but must not destroy the client
		// object before that callback returns.
		using FResponseCallback = std::function<void(std::uint64_t, const Protocol::FRpcResponse&)>;
		using FRequestCallback = std::function<void(std::uint64_t, const Protocol::FRpcRequest&)>;
		using FNotificationCallback = std::function<void(std::uint64_t, const Protocol::FRpcNotification&)>;
		using FSessionCallback = std::function<void(std::uint64_t)>;

		explicit FOutboundRpcClient(FOutboundRpcClientConfig config);
		~FOutboundRpcClient();

		FOutboundRpcClient(const FOutboundRpcClient&) = delete;
		FOutboundRpcClient& operator=(const FOutboundRpcClient&) = delete;
		FOutboundRpcClient(FOutboundRpcClient&&) = delete;
		FOutboundRpcClient& operator=(FOutboundRpcClient&&) = delete;

		// Start begins the reconnect loop; readiness is reported separately by
		// IsReady()/SetReadyCallback after the version-2 Hello handshake succeeds.
		bool Start(std::string& outError);
		void Stop();
		bool IsRunning() const noexcept;
		bool IsReady() const noexcept;
		std::uint64_t GetReadySessionId() const noexcept;
		const FOutboundRpcClientConfig& GetConfig() const noexcept;
		std::string GetLastError() const;

		Session::FRpcSessionRegistry& GetSessionRegistry() noexcept;
		Call::FRpcRequestIdGenerator& GetRequestIdGenerator() noexcept;
		Transport::IRpcTransport& GetTransport() noexcept;

		void SetResponseCallback(FResponseCallback callback);
		void SetRequestCallback(FRequestCallback callback);
		void SetNotificationCallback(FNotificationCallback callback);
		void SetDisconnectCallback(FSessionCallback callback);
		void SetReadyCallback(FSessionCallback callback);

	private:
		struct FImpl;
		std::unique_ptr<FImpl> m_impl;
	};
}
