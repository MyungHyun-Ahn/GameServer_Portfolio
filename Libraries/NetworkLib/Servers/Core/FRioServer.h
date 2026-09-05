#pragma once

namespace NetworkLib
{
	class IApplicationHandler;
}

namespace NetworkLib::Packet::Buffer
{
	class FPacketBuffer;
}

namespace NetworkLib::Session
{
	class FRioSession;
}

namespace NetworkLib::Core
{
	class FRioServer final : public IServer
	{
	public:
		FRioServer();
		~FRioServer() override;

		bool Start(const SServerConfig& serverConfig, IApplicationHandler& applicationHandler) override;
		void Stop() override;
		bool SendPacket(std::uint64_t sessionId, NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet) override;
		bool Disconnect(std::uint64_t sessionId) override;
		EBackendKind GetBackendKind() const override;
		SServerStats GetStatsSnapshot() const override;

	private:
		struct SSendCommand
		{
			std::uint64_t sessionId = 0;
		};

		struct SRioWorker
		{
			HANDLE completionEvent = nullptr;
			RIO_CQ completionQueue = RIO_INVALID_CQ;
			std::thread thread;
			std::atomic<std::uint32_t> activeSessionCount = 0;
			std::atomic<std::uint32_t> maxObservedSendCommandCount = 0;
			std::atomic<std::uint32_t> queuedSendCommandCount = 0;
			NetworkLib::Containers::FLockFreeQueue<std::uint64_t> sendCommands;
		};

	private:
		bool InitializeWinsock();
		bool LoadRioFunctionTable();
		bool LoadAcceptExFunction();
		bool OpenListenSocket();
		void CloseListenSocket();
		bool StartWorkers();
		void StopWorkers();
		void AcceptLoop();
		void WorkerLoop(std::uint32_t workerIndex);
		void DrainSendCommands(std::uint32_t workerIndex);
		bool DrainOwnerThreadSendQueue(NetworkLib::Session::FRioSession& sessionContext);
		bool AppendPacketToSendRing(NetworkLib::Session::FRioSession& sessionContext,
			NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer,
			std::uint64_t sessionId,
			bool lockSendRing);
		bool PostSend(NetworkLib::Session::FRioSession& sessionContext, std::uint64_t sessionId);
		bool SubmitPreparedSend(NetworkLib::Session::FRioSession& sessionContext, std::uint64_t sessionId);
		bool EnqueueOwnerThreadSend(NetworkLib::Session::FRioSession& sessionContext,
			NetworkLib::Packet::Buffer::FPacketBuffer* packetBuffer,
			std::uint64_t sessionId,
			std::uint32_t ownerWorkerIndex);
		bool HasPendingSendCommands(std::uint32_t workerIndex) const;
		bool AttachAcceptedSocket(SOCKET clientSocket);
		bool PostRecv(NetworkLib::Session::FRioSession& sessionContext);
		void HandleRioCompletion(const RIORESULT& completionResult);
		void HandleRecvCompletion(NetworkLib::Session::FRioSession& sessionContext, const RIORESULT& completionResult);
		void HandleSendCompletion(NetworkLib::Session::FRioSession& sessionContext, const RIORESULT& completionResult);
		void CloseSession(NetworkLib::Session::FRioSession& sessionContext);
		void ReleaseSession(NetworkLib::Session::FRioSession* sessionContext) const;
		NetworkLib::Session::FRioSession* AcquireSessionBySlotIndex(std::uint32_t slotIndex) const;
		NetworkLib::Session::FRioSession* AcquireSession(std::uint64_t sessionId);
		std::uint32_t ChooseLeastLoadedWorkerIndex() const noexcept;
		std::uint64_t ComposeSessionId(std::uint32_t slotIndex, std::uint32_t generation) const noexcept;
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
		SServerConfig m_serverConfig{};
		IApplicationHandler* m_applicationHandler = nullptr;
		std::shared_ptr<Foundation::ILogger> m_logger;
		std::shared_ptr<NetworkLib::Crypto::IPacketCipher> m_packetCipher;
		std::shared_ptr<NetworkLib::Packet::Framing::IPacketFramer> m_packetFramer;
		SOCKET m_listenSocket = INVALID_SOCKET;
		std::thread m_acceptThread;
		std::vector<std::unique_ptr<SRioWorker>> m_workers;
		RIO_EXTENSION_FUNCTION_TABLE m_rioFunctionTable{};
		LPFN_ACCEPTEX m_acceptEx = nullptr;
		std::unique_ptr<std::atomic<NetworkLib::Session::FRioSession*>[]> m_sessionSlots;
		std::unique_ptr<std::atomic<std::uint32_t>[]> m_generations;
		std::atomic<std::uint32_t> m_packetRandomKeySeed = 1;
		NetworkLib::Diagnostics::FServerMonitoringRuntime m_monitoring{};
		std::atomic<bool> m_isRunning = false;
		std::atomic<bool> m_winsockInitialized = false;
	};
}
