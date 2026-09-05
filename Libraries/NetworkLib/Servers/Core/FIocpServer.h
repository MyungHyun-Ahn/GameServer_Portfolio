#pragma once

namespace NetworkLib
{
	class IApplicationHandler;
}

namespace NetworkLib::Session
{
	class FIocpSession;
}

namespace NetworkLib::Core
{
	using NetworkLib::IApplicationHandler;

	class FIocpServer final : public IServer
	{
	public:
		FIocpServer();
		~FIocpServer() override;

		bool Start(const SServerConfig& serverConfig, IApplicationHandler& applicationHandler) override;
		void Stop() override;
		bool SendPacket(std::uint64_t sessionId, NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet) override;
		bool Disconnect(std::uint64_t sessionId) override;
		EBackendKind GetBackendKind() const override;
		SServerStats GetStatsSnapshot() const override;

	private:
		struct SAcceptContext
		{
			OVERLAPPED overlapped{};
			SOCKET acceptedSocket = INVALID_SOCKET;
			std::uint32_t slotIndex = 0;
			std::array<char, (sizeof(sockaddr_in) + 16) * 2> buffer{};

			void ResetOverlapped() noexcept
			{
				ZeroMemory(&overlapped, sizeof(overlapped));
			}
		};

	private:
		bool InitializeWinsock();
		bool OpenListenSocket();
		bool LoadAcceptExFunctions();
		bool InitializeAcceptContexts();
		bool PostAccept(std::uint32_t acceptSlotIndex);
		void CloseAcceptContexts() noexcept;
		void CloseListenSocket();
		void StartWorkers();
		void StopWorkers();
		void WorkerLoop();
		bool HandleAcceptCompletion(SAcceptContext& acceptContext, bool completionSucceeded, DWORD completionError);
		bool PostRecv(NetworkLib::Session::FIocpSession& sessionContext);
		bool PostSend(NetworkLib::Session::FIocpSession& sessionContext);
		void CloseSession(NetworkLib::Session::FIocpSession& sessionContext);
		void ReleaseSession(NetworkLib::Session::FIocpSession* sessionContext) const;
		NetworkLib::Session::FIocpSession* AcquireSessionBySlotIndex(std::uint32_t slotIndex) const;
		NetworkLib::Session::FIocpSession* AcquireSession(std::uint64_t sessionId);
		bool AttachAcceptedSocket(SOCKET clientSocket);
		std::uint64_t ComposeSessionId(std::uint32_t slotIndex, std::uint32_t generation) const;
		std::uint8_t GeneratePacketRandomKey() noexcept;
		void Log(Foundation::ELogLevel logLevel, const std::string& message) const;

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			Foundation::ELogLevel logLevel,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(logLevel, "NetworkLib", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		inline static constexpr std::size_t kMaxSendBatchCount = 32;
		inline static constexpr ULONG_PTR kAcceptCompletionKey = 1;
		inline static constexpr std::uint32_t kMinimumAcceptContextCount = 4;
		SServerConfig m_serverConfig{};
		IApplicationHandler* m_applicationHandler = nullptr;
		std::shared_ptr<Foundation::ILogger> m_logger;
		std::shared_ptr<NetworkLib::Crypto::IPacketCipher> m_packetCipher;
		std::shared_ptr<NetworkLib::Packet::Framing::IPacketFramer> m_packetFramer;
		HANDLE m_iocpHandle = nullptr;
		SOCKET m_listenSocket = INVALID_SOCKET;
		std::vector<std::thread> m_workerThreads;
		std::unique_ptr<std::atomic<NetworkLib::Session::FIocpSession*>[]> m_sessionSlots;
		std::unique_ptr<std::atomic<std::uint32_t>[]> m_generations;
		std::mutex m_sessionLifecycleMutex;
		std::unique_ptr<SAcceptContext[]> m_acceptContexts;
		std::uint32_t m_acceptContextCount = 0;
		LPFN_ACCEPTEX m_acceptEx = nullptr;
		LPFN_GETACCEPTEXSOCKADDRS m_getAcceptExSockaddrs = nullptr;
		std::atomic<std::uint32_t> m_packetRandomKeySeed = 1;
		NetworkLib::Diagnostics::FServerMonitoringRuntime m_monitoring{};
		std::atomic<bool> m_isRunning = false;
		std::atomic<bool> m_winsockInitialized = false;
	};
}
