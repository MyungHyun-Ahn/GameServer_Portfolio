#include "NetLibPch.h"

#include "Crypto/IPacketCipher.h"
#include "Packet/Buffer/FPacketBuffer.h"
#include "Packet/Framing/IPacketFramer.h"
#include "Packet/Framing/PacketTypes.h"
#include "Packet/Serialization/FPacketSerialization.h"
#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FRioServer.h"
#include "Servers/IApplicationHandler.h"
#include "Servers/Session/FRioSession.h"
#include "Foundation/Logging/ILogger.h"

#include <format>
#pragma comment(lib, "Ws2_32.lib")

namespace NetworkLib::Core
{
	using NetworkLib::Packet::Buffer::FPacketBuffer;
	using NetworkLib::Packet::Framing::CalculatePacketChecksum;
	using NetworkLib::Packet::Framing::SFramedPacketBufferParts;
	using NetworkLib::Packet::Framing::SOutgoingPacket;
	using NetworkLib::Packet::Framing::SPacketHeader;
	using NetworkLib::Packet::Serialization::TryParseContentPacketView;
	using NetworkLib::Packet::View::FPacketView;
	using NetworkLib::Session::FRioSession;

	namespace
	{
		inline constexpr std::size_t kCompletionBatchSize = 128;
		inline constexpr DWORD kWorkerWaitTimeoutMs = 100;
		inline constexpr ULONG kMaxOutstandingReceive = 1;
		inline constexpr ULONG kMaxReceiveDataBuffers = 1;
		inline constexpr ULONG kMaxOutstandingSend = 8;
		inline constexpr ULONG kMaxSendDataBuffers = 1;

		using FSteadyClock = std::chrono::steady_clock;

		std::uint64_t ToNanoseconds(
			const FSteadyClock::duration duration) noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
		}

		bool ApplyAcceptedSocketSendBufferOption(
			const SServerConfig& serverConfig,
			const SOCKET clientSocket,
			std::string& outError) noexcept
		{
			const int sendBufferBytes = serverConfig.socketSendBufferBytes < 0 ? 0 : serverConfig.socketSendBufferBytes;
			if (setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBufferBytes), sizeof(sendBufferBytes)) ==
				SOCKET_ERROR)
			{
				std::ostringstream oss;
				oss << "setsockopt(SO_SNDBUF) failed. requested=" << sendBufferBytes << " error=" << WSAGetLastError();
				outError = oss.str();
				return false;
			}

			return true;
		}
	}

	FRioServer::FRioServer()
	{
		m_rioFunctionTable.cbSize = sizeof(m_rioFunctionTable);
	}

	FRioServer::~FRioServer()
	{
		Stop();
	}

	bool FRioServer::Start(
		const SServerConfig& serverConfig,
		IApplicationHandler& applicationHandler)
	{
		if (m_isRunning.exchange(true))
		{
			Log(Foundation::ELogLevel::Warn, "Start requested while server is already running.");
			return false;
		}

		m_serverConfig = serverConfig;
		m_applicationHandler = &applicationHandler;
		m_logger = m_serverConfig.logger;
		m_packetCipher = m_serverConfig.packetCipher;
		m_packetFramer = m_serverConfig.packetFramer;
		m_monitoring.Reset();
		FPacketBuffer::ConfigurePageReuse(m_serverConfig.enablePageBufferReuse, m_serverConfig.pageBufferSize);

		if (m_packetCipher != nullptr && m_packetFramer == nullptr)
		{
			Log(Foundation::ELogLevel::Error, "Packet cipher requires packet framer.");
			Stop();
			return false;
		}

		m_sessionSlots = std::make_unique<std::atomic<FRioSession*>[]>(m_serverConfig.maxSessionCount);
		m_generations = std::make_unique<std::atomic<std::uint32_t>[]>(m_serverConfig.maxSessionCount);
		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			m_sessionSlots[slotIndex].store(nullptr);
			m_generations[slotIndex].store(1);
		}

		FRioSession::EnsurePoolCapacity(static_cast<LONG>(m_serverConfig.maxSessionCount));

		if (!InitializeWinsock())
		{
			Log(Foundation::ELogLevel::Error, "Winsock initialization failed.");
			Stop();
			return false;
		}

		if (!LoadRioFunctionTable())
		{
			Log(Foundation::ELogLevel::Error, "RIO function table load failed.");
			Stop();
			return false;
		}

		if (!OpenListenSocket())
		{
			Log(Foundation::ELogLevel::Error, "Listen socket open failed.");
			Stop();
			return false;
		}

		if (!LoadAcceptExFunction())
		{
			Log(Foundation::ELogLevel::Error, "AcceptEx function load failed.");
			Stop();
			return false;
		}

		if (!StartWorkers())
		{
			Log(Foundation::ELogLevel::Error, "RIO worker startup failed.");
			Stop();
			return false;
		}

		m_acceptThread = std::thread(&FRioServer::AcceptLoop, this);
		{
			Log(Foundation::ELogLevel::Info,

				"RIO server started. ip={} port={} workers={} maxSessions={} sendRingBytes={}",
				m_serverConfig.bindIp,
				m_serverConfig.port,
				m_workers.size(),
				m_serverConfig.maxSessionCount,
				m_serverConfig.rioSendRingSizeBytes);
		}
		m_applicationHandler->OnServerStarted(*this);
		return true;
	}

	void FRioServer::Stop()
	{
		if (!m_isRunning.exchange(false))
		{
			return;
		}

		Log(Foundation::ELogLevel::Info, "RIO server stop requested.");

		CloseListenSocket();

		if (m_acceptThread.joinable())
		{
			m_acceptThread.join();
		}

		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			FRioSession* sessionContext = m_sessionSlots[slotIndex].exchange(nullptr);
			if (sessionContext != nullptr)
			{
				CloseSession(*sessionContext);
				ReleaseSession(sessionContext);
			}
		}

		StopWorkers();
		FRioSession::ReleaseAllSendRingRegistrations(m_rioFunctionTable);

		if (m_winsockInitialized.exchange(false))
		{
			WSACleanup();
		}

		if (m_applicationHandler != nullptr)
		{
			m_applicationHandler->OnServerStopped();
			m_applicationHandler = nullptr;
		}

		Log(Foundation::ELogLevel::Info, "RIO server stopped.");
		m_packetCipher.reset();
		m_packetFramer.reset();
		m_acceptEx = nullptr;
		m_logger.reset();
	}

	bool FRioServer::SendPacket(
		std::uint64_t sessionId,
		NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet)
	{
		if (!packet.IsValid())
		{
			Log(Foundation::ELogLevel::Warn, "Send rejected because outgoing packet was invalid.");
			return false;
		}

		const std::int32_t bodyLength = packet.GetBodyLength();
		FRioSession* sessionContext = AcquireSession(sessionId);
		if (sessionContext == nullptr)
		{
			Log(Foundation::ELogLevel::Warn, "Send rejected because session was not found. sessionId={}", sessionId);
			return false;
		}

		FPacketBuffer* packetBuffer = packet.ReleaseBuffer();
		if (packetBuffer == nullptr || packetBuffer->GetBuffer().empty())
		{
			Log(Foundation::ELogLevel::Warn, "RIO send rejected because outgoing packet buffer was empty.");
			FPacketBuffer::Release(packetBuffer);
			ReleaseSession(sessionContext);
			return false;
		}

		const bool sendResult =
			m_serverConfig.rioSendDispatchMode == ERioSendDispatchMode::OwnerThread
				? EnqueueOwnerThreadSend(*sessionContext, packetBuffer, sessionId, sessionContext->GetOwnerWorkerIndex())
				: (AppendPacketToSendRing(*sessionContext, packetBuffer, sessionId, true) && PostSend(*sessionContext, sessionId));
		m_monitoring.OnSendPacket(static_cast<std::uint64_t>(bodyLength > 0 ? bodyLength : 0));
		ReleaseSession(sessionContext);
		return sendResult;
	}

	bool FRioServer::Disconnect(
		std::uint64_t sessionId)
	{
		FRioSession* sessionContext = AcquireSession(sessionId);
		if (sessionContext == nullptr)
		{
			return false;
		}

		CloseSession(*sessionContext);
		ReleaseSession(sessionContext);
		return true;
	}

	EBackendKind FRioServer::GetBackendKind() const
	{
		return EBackendKind::Rio;
	}

	SServerStats FRioServer::GetStatsSnapshot() const
	{
		NetworkLib::Diagnostics::SServerMonitoringSnapshotInput snapshotInput{};
		snapshotInput.pools.sessionPoolCapacity = static_cast<std::uint32_t>(FRioSession::GetPoolCapacity());
		snapshotInput.pools.sessionPoolUsage = static_cast<std::uint32_t>(FRioSession::GetPoolUsage());
		snapshotInput.pools.sendBufferPoolCapacity = 0;
		snapshotInput.pools.sendBufferPoolUsage = 0;
		snapshotInput.pools.packetBufferPoolCapacity = static_cast<std::uint32_t>(FPacketBuffer::GetPoolCapacity());
		snapshotInput.pools.packetBufferPoolUsage = static_cast<std::uint32_t>(FPacketBuffer::GetPoolUsage());

		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			FRioSession* sessionContext = m_sessionSlots[slotIndex].load(std::memory_order_relaxed);
			if (sessionContext == nullptr)
			{
				continue;
			}

			snapshotInput.session.queuedSendBufferCount += sessionContext->GetQueuedSendBufferCount();
			snapshotInput.session.maxObservedQueuedSendBufferCount = std::max<std::uint64_t>(
				snapshotInput.session.maxObservedQueuedSendBufferCount, sessionContext->GetMaxObservedQueuedSendBufferCount());
			snapshotInput.session.ownerQueuedSendBytes += sessionContext->GetOwnerQueuedSendBytes();
			snapshotInput.session.maxObservedOwnerQueuedSendBytes = std::max<std::uint64_t>(
				snapshotInput.session.maxObservedOwnerQueuedSendBytes, sessionContext->GetMaxObservedOwnerQueuedSendBytes());
			snapshotInput.session.totalSendRingUsedBytes += sessionContext->GetSendRingUsedBytes();
			snapshotInput.session.totalSendRingInFlightBytes += sessionContext->GetSendRingInFlightBytes();
			snapshotInput.session.maxCurrentSendRingUsedBytes =
				std::max<std::uint32_t>(snapshotInput.session.maxCurrentSendRingUsedBytes, sessionContext->GetSendRingUsedBytes());
			snapshotInput.session.maxObservedSendRingUsedBytes = std::max<std::uint32_t>(
				snapshotInput.session.maxObservedSendRingUsedBytes, sessionContext->GetMaxObservedSendRingUsedBytes());
		}
		return m_monitoring.BuildSnapshot(snapshotInput);
	}

	bool FRioServer::InitializeWinsock()
	{
		WSADATA wsaData{};
		const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (startupResult != 0)
		{
			Log(Foundation::ELogLevel::Error, "WSAStartup failed. error={}", startupResult);
			return false;
		}

		m_winsockInitialized = true;
		return true;
	}

	bool FRioServer::LoadRioFunctionTable()
	{
		SOCKET tempSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
		if (tempSocket == INVALID_SOCKET)
		{
			Log(Foundation::ELogLevel::Error, "WSASocketW for RIO function table failed. error={}", WSAGetLastError());
			return false;
		}

		GUID rioGuid = WSAID_MULTIPLE_RIO;
		DWORD bytesReturned = 0;
		m_rioFunctionTable = {};
		m_rioFunctionTable.cbSize = sizeof(m_rioFunctionTable);
		const int ioctlResult = WSAIoctl(tempSocket,
			SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
			&rioGuid,
			sizeof(rioGuid),
			&m_rioFunctionTable,
			sizeof(m_rioFunctionTable),
			&bytesReturned,
			nullptr,
			nullptr);
		closesocket(tempSocket);
		if (ioctlResult == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "WSAIoctl(SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER) failed. error={}", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool FRioServer::OpenListenSocket()
	{
		m_listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
		if (m_listenSocket == INVALID_SOCKET)
		{
			Log(Foundation::ELogLevel::Error, "WSASocketW failed. error={}", WSAGetLastError());
			return false;
		}

		sockaddr_in listenAddress{};
		listenAddress.sin_family = AF_INET;
		listenAddress.sin_port = htons(m_serverConfig.port);
		if (InetPtonA(AF_INET, m_serverConfig.bindIp.c_str(), &listenAddress.sin_addr) != 1)
		{
			Log(Foundation::ELogLevel::Error, "InetPtonA failed for bind ip. ip={}", m_serverConfig.bindIp);
			return false;
		}

		BOOL reuseAddress = TRUE;
		setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

		if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "bind failed. error={}", WSAGetLastError());
			return false;
		}

		if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "listen failed. error={}", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool FRioServer::LoadAcceptExFunction()
	{
		GUID acceptExGuid = WSAID_ACCEPTEX;
		DWORD bytesReturned = 0;
		const int ioctlResult = WSAIoctl(m_listenSocket,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&acceptExGuid,
			sizeof(acceptExGuid),
			&m_acceptEx,
			sizeof(m_acceptEx),
			&bytesReturned,
			nullptr,
			nullptr);
		if (ioctlResult == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER, AcceptEx) failed. error={}", WSAGetLastError());
			return false;
		}

		return m_acceptEx != nullptr;
	}

	void FRioServer::CloseListenSocket()
	{
		if (m_listenSocket != INVALID_SOCKET)
		{
			closesocket(m_listenSocket);
			m_listenSocket = INVALID_SOCKET;
		}
	}

	bool FRioServer::StartWorkers()
	{
		const std::uint32_t workerCount = std::max(1u, m_serverConfig.workerThreadCount);
		m_workers.clear();
		m_workers.reserve(workerCount);
		const DWORD completionQueueSize = std::max<DWORD>(256, static_cast<DWORD>(std::max(1u, m_serverConfig.maxSessionCount) * 8u));

		for (std::uint32_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			auto worker = std::make_unique<SRioWorker>();
			worker->completionEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
			if (worker->completionEvent == nullptr)
			{
				Log(Foundation::ELogLevel::Error,
					"CreateEvent failed for RIO worker. workerIndex={} error={}",
					workerIndex,
					GetLastError());
				return false;
			}

			RIO_NOTIFICATION_COMPLETION notificationCompletion{};
			notificationCompletion.Type = RIO_EVENT_COMPLETION;
			notificationCompletion.Event.EventHandle = worker->completionEvent;
			notificationCompletion.Event.NotifyReset = TRUE;
			worker->completionQueue = m_rioFunctionTable.RIOCreateCompletionQueue(completionQueueSize, &notificationCompletion);
			if (worker->completionQueue == RIO_INVALID_CQ)
			{
				Log(Foundation::ELogLevel::Error,
					"RIOCreateCompletionQueue failed. workerIndex={} error={}",
					workerIndex,
					WSAGetLastError());
				return false;
			}

			m_workers.push_back(std::move(worker));
		}

		for (std::uint32_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			m_workers[workerIndex]->thread = std::thread(&FRioServer::WorkerLoop, this, workerIndex);
		}

		return true;
	}

	void FRioServer::StopWorkers()
	{
		const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (FRioSession::GetPoolUsage() > 0 && std::chrono::steady_clock::now() < waitDeadline)
		{
			for (const auto& worker : m_workers)
			{
				if (worker != nullptr && worker->completionEvent != nullptr)
				{
					SetEvent(worker->completionEvent);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		for (const auto& worker : m_workers)
		{
			if (worker != nullptr && worker->completionEvent != nullptr)
			{
				SetEvent(worker->completionEvent);
			}
		}

		for (const auto& worker : m_workers)
		{
			if (worker != nullptr && worker->thread.joinable())
			{
				worker->thread.join();
			}
		}

		for (const auto& worker : m_workers)
		{
			if (worker == nullptr)
			{
				continue;
			}

			std::uint64_t discardedSessionId = 0;
			while (worker->sendCommands.Dequeue(&discardedSessionId))
			{
				worker->queuedSendCommandCount.fetch_sub(1, std::memory_order_relaxed);
			}
		}

		for (const auto& worker : m_workers)
		{
			if (worker == nullptr)
			{
				continue;
			}

			if (worker->completionQueue != RIO_INVALID_CQ)
			{
				m_rioFunctionTable.RIOCloseCompletionQueue(worker->completionQueue);
			}

			if (worker->completionEvent != nullptr)
			{
				CloseHandle(worker->completionEvent);
			}
		}

		m_workers.clear();
	}

	void FRioServer::AcceptLoop()
	{
		while (m_isRunning.load(std::memory_order_acquire))
		{
			SOCKET clientSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
			if (clientSocket == INVALID_SOCKET)
			{
				Log(Foundation::ELogLevel::Warn, "WSASocketW failed for accept socket. error={}", WSAGetLastError());
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			std::array<char, (sizeof(sockaddr_storage) + 16) * 2> addressBuffer{};
			WSAOVERLAPPED acceptOverlapped{};
			acceptOverlapped.hEvent = WSACreateEvent();
			if (acceptOverlapped.hEvent == WSA_INVALID_EVENT)
			{
				Log(Foundation::ELogLevel::Warn, "WSACreateEvent failed for AcceptEx. error={}", WSAGetLastError());
				closesocket(clientSocket);
				continue;
			}

			DWORD bytesReceived = 0;
			BOOL acceptResult = m_acceptEx(m_listenSocket,
				clientSocket,
				addressBuffer.data(),
				0,
				sizeof(sockaddr_storage) + 16,
				sizeof(sockaddr_storage) + 16,
				&bytesReceived,
				&acceptOverlapped);
			if (acceptResult == FALSE)
			{
				const int errorCode = WSAGetLastError();
				if (errorCode != ERROR_IO_PENDING)
				{
					Log(Foundation::ELogLevel::Warn, "AcceptEx failed. error={}", errorCode);
					WSACloseEvent(acceptOverlapped.hEvent);
					closesocket(clientSocket);
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

				DWORD transferredBytes = 0;
				DWORD completionFlags = 0;
				bool waitSucceeded = false;
				while (m_isRunning.load(std::memory_order_acquire))
				{
					const DWORD waitResult = WSAWaitForMultipleEvents(1, &acceptOverlapped.hEvent, TRUE, 100, FALSE);
					if (waitResult == WSA_WAIT_TIMEOUT)
					{
						continue;
					}

					if (waitResult == WSA_WAIT_EVENT_0)
					{
						waitSucceeded =
							WSAGetOverlappedResult(m_listenSocket, &acceptOverlapped, &transferredBytes, FALSE, &completionFlags) == TRUE;
						break;
					}

					Log(Foundation::ELogLevel::Warn,
						"WSAWaitForMultipleEvents failed for AcceptEx. result={} error={}",
						waitResult,
						WSAGetLastError());
					break;
				}

				if (!m_isRunning.load(std::memory_order_acquire))
				{
					WSACloseEvent(acceptOverlapped.hEvent);
					closesocket(clientSocket);
					break;
				}

				if (!waitSucceeded)
				{
					WSACloseEvent(acceptOverlapped.hEvent);
					closesocket(clientSocket);
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
			}

			if (setsockopt(clientSocket,
					SOL_SOCKET,
					SO_UPDATE_ACCEPT_CONTEXT,
					reinterpret_cast<const char*>(&m_listenSocket),
					sizeof(m_listenSocket)) == SOCKET_ERROR)
			{
				Log(Foundation::ELogLevel::Warn, "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed. error={}", WSAGetLastError());
				WSACloseEvent(acceptOverlapped.hEvent);
				closesocket(clientSocket);
				continue;
			}

			WSACloseEvent(acceptOverlapped.hEvent);
			if (!AttachAcceptedSocket(clientSocket))
			{
				Log(Foundation::ELogLevel::Warn, "Accepted socket was rejected because no session slot was available.");
				closesocket(clientSocket);
			}
		}
	}

	void FRioServer::WorkerLoop(
		std::uint32_t workerIndex)
	{
		SRioWorker& worker = *m_workers[workerIndex];
		std::array<RIORESULT, kCompletionBatchSize> completionResults{};
		bool notificationArmed = false;

		try
		{
			while (true)
			{
				DrainSendCommands(workerIndex);
				if (!notificationArmed)
				{
					if (m_rioFunctionTable.RIONotify(worker.completionQueue) != SOCKET_ERROR)
					{
						notificationArmed = true;
					}
				}

				const DWORD waitResult = WaitForSingleObject(worker.completionEvent, kWorkerWaitTimeoutMs);
				if (waitResult == WAIT_OBJECT_0)
				{
					notificationArmed = false;
					while (true)
					{
						const ULONG completionCount = m_rioFunctionTable.RIODequeueCompletion(
							worker.completionQueue, completionResults.data(), static_cast<ULONG>(completionResults.size()));
						if (completionCount == 0)
						{
							break;
						}

						for (ULONG completionIndex = 0; completionIndex < completionCount; ++completionIndex)
						{
							HandleRioCompletion(completionResults[completionIndex]);
						}
					}
				}
				else if (waitResult != WAIT_TIMEOUT)
				{
					Log(Foundation::ELogLevel::Warn,
						"WaitForSingleObject failed in RIO worker. workerIndex={} error={}",
						workerIndex,
						GetLastError());
				}

				DrainSendCommands(workerIndex);
				if (!m_isRunning.load(std::memory_order_acquire) && m_monitoring.GetActiveSessionCount(std::memory_order_acquire) == 0 &&
					FRioSession::GetPoolUsage() == 0 && !HasPendingSendCommands(workerIndex))
				{
					break;
				}
			}
		}
		catch (const std::exception& exception)
		{
			Log(Foundation::ELogLevel::Error,
				"Unhandled std::exception in RIO worker loop. workerIndex={} message={} maxQueuedSendCommands={}",
				workerIndex,
				exception.what(),
				worker.maxObservedSendCommandCount.load(std::memory_order_relaxed));
			throw;
		}
		catch (...)
		{
			Log(Foundation::ELogLevel::Error,
				"Unhandled unknown exception in RIO worker loop. workerIndex={} maxQueuedSendCommands={}",
				workerIndex,
				worker.maxObservedSendCommandCount.load(std::memory_order_relaxed));
			throw;
		}
	}

	void FRioServer::DrainSendCommands(
		std::uint32_t workerIndex)
	{
		if (workerIndex >= m_workers.size())
		{
			return;
		}

		std::uint64_t sessionId = 0;
		while (m_workers[workerIndex]->sendCommands.Dequeue(&sessionId))
		{
			m_workers[workerIndex]->queuedSendCommandCount.fetch_sub(1, std::memory_order_relaxed);

			FRioSession* sessionContext = AcquireSession(sessionId);
			if (sessionContext == nullptr)
			{
				continue;
			}

			sessionContext->ClearOwnerSendDrainScheduled();

			if (sessionContext->GetOwnerWorkerIndex() != workerIndex)
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO owner-thread send command rejected because worker ownership mismatched. "
					"sessionId={} expectedWorkerIndex={} actualWorkerIndex={}",
					sessionId,
					sessionContext->GetOwnerWorkerIndex(),
					workerIndex);
				ReleaseSession(sessionContext);
				continue;
			}

			DrainOwnerThreadSendQueue(*sessionContext);
			ReleaseSession(sessionContext);
		}
	}

	bool FRioServer::DrainOwnerThreadSendQueue(
		FRioSession& sessionContext)
	{
		if (sessionContext.IsClosing())
		{
			return false;
		}

		// Keep one RIOSend outstanding per session. While it is in flight, the
		// bounded owner queue is the next batch and is drained by completion.
		if (sessionContext.HasPreparedSend())
		{
			return true;
		}

		FPacketBuffer* packetBuffer = nullptr;
		while (sessionContext.GetSendRingFreeBytes() >= FRioSession::kMaxSendPacketSizeBytes &&
			   sessionContext.TryDequeueOwnerSendPacket(packetBuffer))
		{
			if (!AppendPacketToSendRing(sessionContext, packetBuffer, sessionContext.GetSessionId(), false))
			{
				return false;
			}
		}

		// Flush either when the handoff queue is empty or when less than one
		// maximum-sized packet remains. Any queued remainder becomes the next batch.
		return PostSend(sessionContext, sessionContext.GetSessionId());
	}

	bool FRioServer::AppendPacketToSendRing(
		FRioSession& sessionContext,
		FPacketBuffer* packetBuffer,
		const std::uint64_t sessionId,
		const bool lockSendRing)
	{
		enum class EAppendFailureReason
		{
			None,
			Closing,
			EmptyPayload,
			BuildPacketPartsFailed,
			ZeroLengthPacket,
			OversizePacket,
			SendStall,
		};

		struct SAppendFailureContext
		{
			EAppendFailureReason reason = EAppendFailureReason::None;
			std::size_t packetBytes = 0;
			std::uint32_t usedBytes = 0;
			std::uint32_t freeBytes = 0;
			std::uint32_t inFlightBytes = 0;
			std::uint32_t maxObservedUsedBytes = 0;
		};

		if (packetBuffer == nullptr)
		{
			Log(Foundation::ELogLevel::Warn, "RIO send rejected because packet buffer was null.");
			return false;
		}

		bool appendSucceeded = false;
		SAppendFailureContext failureContext{};
		auto appendOperation = [&]()
		{
			m_monitoring.GetRioSendMetrics().RecordSendRingTouch(sessionContext);

			if (sessionContext.IsClosing())
			{
				failureContext.reason = EAppendFailureReason::Closing;
				return;
			}

			std::vector<char>& payloadBuffer = packetBuffer->GetBuffer();
			if (payloadBuffer.empty())
			{
				failureContext.reason = EAppendFailureReason::EmptyPayload;
				return;
			}

			const auto prepareStart = FSteadyClock::now();
			std::uint8_t randomKey = 0;
			if (m_packetCipher != nullptr)
			{
				randomKey = GeneratePacketRandomKey();
				m_packetCipher->Encode(payloadBuffer.data(), static_cast<int>(payloadBuffer.size()), randomKey);
			}

			const char* headerData = nullptr;
			std::size_t headerLength = 0;
			if (m_packetFramer != nullptr)
			{
				SOutgoingPacket outgoingPacket{};
				outgoingPacket.randomKey = randomKey;
				outgoingPacket.checkSum = CalculatePacketChecksum(payloadBuffer.data(), static_cast<std::int32_t>(payloadBuffer.size()));
				outgoingPacket.payload = payloadBuffer.data();
				outgoingPacket.payloadLength = static_cast<std::int32_t>(payloadBuffer.size());

				SFramedPacketBufferParts packetParts{};
				if (!m_packetFramer->BuildPacketParts(outgoingPacket, packetParts))
				{
					failureContext.reason = EAppendFailureReason::BuildPacketPartsFailed;
					return;
				}

				headerData = packetParts.headerBytes.data();
				headerLength = packetParts.headerLength;
			}

			const std::size_t payloadLength = payloadBuffer.size();
			const std::size_t packetLength = headerLength + payloadLength;
			m_monitoring.GetRioSendMetrics().RecordSendPrepareSample(ToNanoseconds(FSteadyClock::now() - prepareStart));
			failureContext.packetBytes = packetLength;
			if (packetLength == 0)
			{
				failureContext.reason = EAppendFailureReason::ZeroLengthPacket;
				return;
			}

			if (packetLength > FRioSession::kMaxSendPacketSizeBytes)
			{
				failureContext.reason = EAppendFailureReason::OversizePacket;
				return;
			}

			if (!sessionContext.TryAppendSendPacket(headerData, headerLength, payloadBuffer.data(), payloadLength))
			{
				failureContext.reason = EAppendFailureReason::SendStall;
				failureContext.usedBytes = sessionContext.GetSendRingUsedBytes();
				failureContext.freeBytes = sessionContext.GetSendRingFreeBytes();
				failureContext.inFlightBytes = sessionContext.GetSendRingInFlightBytes();
				failureContext.maxObservedUsedBytes = sessionContext.GetMaxObservedSendRingUsedBytes();
				return;
			}

			appendSucceeded = true;
		};

		if (lockSendRing)
		{
			const auto waitStart = FSteadyClock::now();
			std::unique_lock<std::mutex> sendRingLock(sessionContext.GetSendRingMutex());
			const auto lockAcquired = FSteadyClock::now();
			appendOperation();
			const auto lockReleased = FSteadyClock::now();
			sendRingLock.unlock();
			m_monitoring.GetRioSendMetrics().RecordDirectSendRingLockSample(
				ToNanoseconds(lockAcquired - waitStart), ToNanoseconds(lockReleased - lockAcquired));
		}
		else
		{
			appendOperation();
		}

		FPacketBuffer::Release(packetBuffer);
		switch (failureContext.reason)
		{
			case EAppendFailureReason::None:
			case EAppendFailureReason::Closing:
				break;

			case EAppendFailureReason::EmptyPayload:
				Log(Foundation::ELogLevel::Warn, "RIO send rejected because packet payload was empty.");
				break;

			case EAppendFailureReason::BuildPacketPartsFailed:
				Log(Foundation::ELogLevel::Error, "BuildPacketParts failed during RIO send ring append.");
				break;

			case EAppendFailureReason::ZeroLengthPacket:
				Log(Foundation::ELogLevel::Warn, "RIO send rejected because framed packet length was zero.");
				break;

			case EAppendFailureReason::OversizePacket:
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO send rejected because packet exceeded max send packet size. sessionId={} packetBytes={} maxPacketBytes={}",
					sessionId,
					failureContext.packetBytes,
					FRioSession::kMaxSendPacketSizeBytes);
				CloseSession(sessionContext);
				break;
			}

			case EAppendFailureReason::SendStall:
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO send stall detected because session send ring was full. sessionId={} ringBytes={} packetBytes={} "
					"usedBytes={} freeBytes={} inFlightBytes={} maxObservedUsedBytes={}",
					sessionId,
					sessionContext.GetSendRingCapacityBytes(),
					failureContext.packetBytes,
					failureContext.usedBytes,
					failureContext.freeBytes,
					failureContext.inFlightBytes,
					failureContext.maxObservedUsedBytes);
				CloseSession(sessionContext);
				break;
			}
		}

		return appendSucceeded;
	}

	bool FRioServer::PostSend(
		FRioSession& sessionContext,
		const std::uint64_t sessionId)
	{
		bool preparedSend = false;
		if (m_serverConfig.rioSendDispatchMode == ERioSendDispatchMode::Direct)
		{
			const auto waitStart = FSteadyClock::now();
			std::unique_lock<std::mutex> sendRingLock(sessionContext.GetSendRingMutex());
			const auto lockAcquired = FSteadyClock::now();
			m_monitoring.GetRioSendMetrics().RecordSendRingTouch(sessionContext);
			preparedSend = sessionContext.TryPrepareNextSend();
			const auto lockReleased = FSteadyClock::now();
			sendRingLock.unlock();
			m_monitoring.GetRioSendMetrics().RecordDirectSendRingLockSample(
				ToNanoseconds(lockAcquired - waitStart), ToNanoseconds(lockReleased - lockAcquired));
		}

		else
		{
			m_monitoring.GetRioSendMetrics().RecordSendRingTouch(sessionContext);
			preparedSend = sessionContext.TryPrepareNextSend();
		}

		if (!preparedSend)
		{
			return true;
		}

		return SubmitPreparedSend(sessionContext, sessionId);
	}

	bool FRioServer::SubmitPreparedSend(
		FRioSession& sessionContext,
		const std::uint64_t sessionId)
	{
		FRioSession::SSendRequestContext& sendRequestContext = sessionContext.GetSendRequestContext();
		bool sendResult = false;
		{
			std::scoped_lock<std::mutex> requestQueueLock(sessionContext.GetRequestQueueMutex());
			if (!sessionContext.IsClosing() && sessionContext.GetRequestQueue() != RIO_INVALID_RQ)
			{
				sessionContext.AcquireRef();
				sendResult = m_rioFunctionTable.RIOSend(
								 sessionContext.GetRequestQueue(), &sendRequestContext.buffer, 1, 0, &sendRequestContext) == TRUE;
				if (!sendResult)
				{
					ReleaseSession(&sessionContext);
				}
			}
		}

		if (!sendResult)
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Warn, "RIOSend failed. sessionId={} error={}", sessionId, errorCode);
			if (m_serverConfig.rioSendDispatchMode == ERioSendDispatchMode::Direct)
			{
				std::scoped_lock<std::mutex> sendRingLock(sessionContext.GetSendRingMutex());
				sessionContext.CancelPreparedSend();
			}
			else
			{
				sessionContext.CancelPreparedSend();
			}

			if (!sessionContext.IsClosing())
			{
				CloseSession(sessionContext);
			}
			return false;
		}

		return true;
	}

	bool FRioServer::EnqueueOwnerThreadSend(
		FRioSession& sessionContext,
		FPacketBuffer* packetBuffer,
		const std::uint64_t sessionId,
		const std::uint32_t ownerWorkerIndex)
	{
		if (packetBuffer == nullptr)
		{
			Log(Foundation::ELogLevel::Warn, "RIO owner-thread send rejected because packet buffer was null.");
			return false;
		}

		if (ownerWorkerIndex >= m_workers.size())
		{
			Log(Foundation::ELogLevel::Warn,
				"RIO owner-thread send rejected because worker index was invalid. sessionId={} workerIndex={}",
				sessionId,
				ownerWorkerIndex);
			FPacketBuffer::Release(packetBuffer);
			return false;
		}

		try
		{
			if (!sessionContext.TryEnqueueOwnerSendPacket(packetBuffer))
			{
				const std::uint64_t packetBytes = packetBuffer->GetBuffer().size();
				Log(Foundation::ELogLevel::Warn,
					"RIO owner-thread slow consumer detected because queued send bytes exceeded "
					"the limit. sessionId={} queuedBytes={} packetBytes={} queueLimitBytes={}",
					sessionId,
					sessionContext.GetOwnerQueuedSendBytes(),
					packetBytes,
					sessionContext.GetSendRingCapacityBytes());
				FPacketBuffer::Release(packetBuffer);
				CloseSession(sessionContext);
				return false;
			}
			if (!sessionContext.TryScheduleOwnerSendDrain())
			{
				return true;
			}

			m_workers[ownerWorkerIndex]->sendCommands.Enqueue(sessionId);
			const std::uint32_t queuedCommandCount =
				m_workers[ownerWorkerIndex]->queuedSendCommandCount.fetch_add(1, std::memory_order_relaxed) + 1;

			std::uint32_t observedMax = m_workers[ownerWorkerIndex]->maxObservedSendCommandCount.load(std::memory_order_relaxed);
			while (queuedCommandCount > observedMax && !m_workers[ownerWorkerIndex]->maxObservedSendCommandCount.compare_exchange_weak(
														   observedMax, queuedCommandCount, std::memory_order_relaxed))
			{
			}

			if (queuedCommandCount >= 8192 && (queuedCommandCount % 8192) == 0)
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO owner-thread send queue is growing. workerIndex={} queuedSendCommands={} activeSessions={}",
					ownerWorkerIndex,
					queuedCommandCount,
					m_workers[ownerWorkerIndex]->activeSessionCount.load(std::memory_order_relaxed));
			}
		}
		catch (const std::exception& exception)
		{
			Log(Foundation::ELogLevel::Error,
				"RIO owner-thread send enqueue failed. workerIndex={} sessionId={} message={} maxQueuedSendCommands={}",
				ownerWorkerIndex,
				sessionId,
				exception.what(),
				m_workers[ownerWorkerIndex]->maxObservedSendCommandCount.load(std::memory_order_relaxed));
			sessionContext.ClearOwnerSendDrainScheduled();
			CloseSession(sessionContext);
			return false;
		}
		catch (...)
		{
			Log(Foundation::ELogLevel::Error,

				"RIO owner-thread send enqueue failed with unknown exception. workerIndex={} sessionId={} maxQueuedSendCommands={}",
				ownerWorkerIndex,
				sessionId,
				m_workers[ownerWorkerIndex]->maxObservedSendCommandCount.load(std::memory_order_relaxed));
			sessionContext.ClearOwnerSendDrainScheduled();
			CloseSession(sessionContext);
			return false;
		}

		if (m_workers[ownerWorkerIndex]->completionEvent != nullptr)
		{
			SetEvent(m_workers[ownerWorkerIndex]->completionEvent);
		}

		return true;
	}

	bool FRioServer::HasPendingSendCommands(
		const std::uint32_t workerIndex) const
	{
		if (workerIndex >= m_workers.size())
		{
			return false;
		}

		return m_workers[workerIndex]->queuedSendCommandCount.load(std::memory_order_relaxed) > 0;
	}

	bool FRioServer::AttachAcceptedSocket(
		SOCKET clientSocket)
	{
		{
			std::string errorMessage;
			if (!ApplyAcceptedSocketSendBufferOption(m_serverConfig, clientSocket, errorMessage))
			{
				Log(Foundation::ELogLevel::Error, errorMessage);
				return false;
			}
		}

		const std::uint32_t workerIndex = ChooseLeastLoadedWorkerIndex();
		const std::size_t recvBufferCapacity =
			static_cast<std::size_t>(std::max<std::uint32_t>(m_serverConfig.recvBufferSize * 8u, 65536u));
		const std::size_t recvStagingCapacity = static_cast<std::size_t>(std::max<std::uint32_t>(m_serverConfig.recvBufferSize, 2048u));
		std::optional<std::uint32_t> freeSlotIndex;
		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			if (m_sessionSlots[slotIndex].load(std::memory_order_acquire) == nullptr)
			{
				freeSlotIndex = slotIndex;
				break;
			}
		}

		if (!freeSlotIndex.has_value())
		{
			Log(Foundation::ELogLevel::Warn, "All session slots are in use.");
			return false;
		}

		const std::uint32_t slotIndex = *freeSlotIndex;
		const std::uint32_t generation = m_generations[slotIndex].fetch_add(1);
		const std::uint64_t sessionId = ComposeSessionId(slotIndex, generation);
		FRioSession* newSessionContext = FRioSession::Create();
		newSessionContext->Initialize(clientSocket,
			sessionId,
			slotIndex,
			generation,
			workerIndex,
			recvBufferCapacity,
			recvStagingCapacity,
			static_cast<std::size_t>(std::max<std::uint32_t>(
				static_cast<std::uint32_t>(FRioSession::kMaxSendPacketSizeBytes), m_serverConfig.rioSendRingSizeBytes)));

		if (!newSessionContext->EnsureSendRingRegistered(m_rioFunctionTable))
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Error,
				"RIORegisterBuffer failed for session send ring. sessionId={} error={}",
				sessionId,
				errorCode);
			FRioSession::Destroy(newSessionContext);
			return false;
		}

		const RIO_BUFFERID recvBufferId = m_rioFunctionTable.RIORegisterBuffer(
			newSessionContext->GetRecvStagingData(), static_cast<DWORD>(newSessionContext->GetRecvStagingCapacity()));
		if (recvBufferId == RIO_INVALID_BUFFERID)
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Error, "RIORegisterBuffer failed for recv staging buffer. error={}", errorCode);
			FRioSession::Destroy(newSessionContext);
			return false;
		}
		newSessionContext->SetRecvBufferId(recvBufferId);

		RIO_RQ requestQueue = m_rioFunctionTable.RIOCreateRequestQueue(clientSocket,
			kMaxOutstandingReceive,
			kMaxReceiveDataBuffers,
			kMaxOutstandingSend,
			kMaxSendDataBuffers,
			m_workers[workerIndex]->completionQueue,
			m_workers[workerIndex]->completionQueue,
			reinterpret_cast<PVOID>(newSessionContext));
		if (requestQueue == RIO_INVALID_RQ)
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Error, "RIOCreateRequestQueue failed. error={}", errorCode);
			newSessionContext->ReleaseRioResources(m_rioFunctionTable);
			FRioSession::Destroy(newSessionContext);
			return false;
		}
		newSessionContext->SetRequestQueue(requestQueue);

		FRioSession* expected = nullptr;
		if (!m_sessionSlots[slotIndex].compare_exchange_strong(expected, newSessionContext))
		{
			Log(Foundation::ELogLevel::Warn, "Free session slot was lost before RIO session attach completed.");
			newSessionContext->ReleaseRioResources(m_rioFunctionTable);
			FRioSession::Destroy(newSessionContext);
			return false;
		}

		{
			Log(Foundation::ELogLevel::Info,
				"Client connected. sessionId={} workerIndex={}",
				newSessionContext->GetSessionId(),
				workerIndex);
		}
		m_workers[workerIndex]->activeSessionCount.fetch_add(1, std::memory_order_relaxed);
		m_monitoring.OnSessionAccepted();
		m_applicationHandler->OnClientConnected(newSessionContext->GetSessionId());
		if (!PostRecv(*newSessionContext))
		{
			CloseSession(*newSessionContext);
			ReleaseSession(newSessionContext);
			return false;
		}

		return true;
	}

	bool FRioServer::PostRecv(
		FRioSession& sessionContext)
	{
		if (sessionContext.IsClosing())
		{
			return false;
		}

		if (!sessionContext.TryBeginRecv())
		{
			return false;
		}

		const ULONG recvLength = static_cast<ULONG>(
			std::min<std::size_t>(sessionContext.GetRecvBuffer().GetFreeSize(), sessionContext.GetRecvStagingCapacity()));
		if (recvLength == 0)
		{
			sessionContext.EndRecv();
			Log(Foundation::ELogLevel::Warn, "RIO receive rejected because no writable space is available.");
			return false;
		}

		auto& recvRequestContext = sessionContext.GetRecvRequestContext();
		recvRequestContext.requestKind = FRioSession::ERequestKind::Recv;
		recvRequestContext.ownerSession = &sessionContext;
		recvRequestContext.buffer.BufferId = sessionContext.GetRecvBufferId();
		recvRequestContext.buffer.Offset = 0;
		recvRequestContext.buffer.Length = recvLength;

		bool recvResult = false;
		{
			std::scoped_lock<std::mutex> requestQueueLock(sessionContext.GetRequestQueueMutex());
			if (!sessionContext.IsClosing() && sessionContext.GetRequestQueue() != RIO_INVALID_RQ)
			{
				sessionContext.AcquireRef();
				recvResult = m_rioFunctionTable.RIOReceive(
								 sessionContext.GetRequestQueue(), &recvRequestContext.buffer, 1, 0, &recvRequestContext) == TRUE;
				if (!recvResult)
				{
					ReleaseSession(&sessionContext);
				}
			}
		}

		if (!recvResult)
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Warn, "RIOReceive failed. sessionId={} error={}", sessionContext.GetSessionId(), errorCode);
			sessionContext.EndRecv();
			return false;
		}

		return true;
	}

	void FRioServer::HandleRioCompletion(
		const RIORESULT& completionResult)
	{
		auto* requestContext = reinterpret_cast<FRioSession::SRequestContext*>(static_cast<ULONG_PTR>(completionResult.RequestContext));
		if (requestContext == nullptr || requestContext->ownerSession == nullptr)
		{
			return;
		}

		FRioSession& sessionContext = *requestContext->ownerSession;
		switch (requestContext->requestKind)
		{
			case FRioSession::ERequestKind::Recv:
				sessionContext.EndRecv();
				HandleRecvCompletion(sessionContext, completionResult);
				ReleaseSession(&sessionContext);
				break;
			case FRioSession::ERequestKind::Send:
				HandleSendCompletion(sessionContext, completionResult);
				ReleaseSession(&sessionContext);
				break;
			default:
				break;
		}
	}

	void FRioServer::HandleRecvCompletion(
		FRioSession& sessionContext,
		const RIORESULT& completionResult)
	{
		if (completionResult.Status != ERROR_SUCCESS || completionResult.BytesTransferred == 0)
		{
			if (completionResult.Status != ERROR_SUCCESS)
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO recv completion failed. sessionId={} status={}",
					sessionContext.GetSessionId(),
					completionResult.Status);
			}
			CloseSession(sessionContext);
			return;
		}

		m_monitoring.OnReceiveBytes(completionResult.BytesTransferred);
		if (!sessionContext.CopyReceivedDataFromStaging(completionResult.BytesTransferred))
		{
			Log(Foundation::ELogLevel::Warn, "RIO recv staging copy failed.");
			CloseSession(sessionContext);
			return;
		}

		if (m_packetFramer != nullptr)
		{
			while (true)
			{
				FPacketView packetView;
				if (!m_packetFramer->TryExtractPacketView(sessionContext.GetRecvBuffer(), packetView))
				{
					break;
				}

				const std::uint8_t actualChecksum = CalculatePacketChecksum(packetView.payload, packetView.payloadLength);
				if (actualChecksum != packetView.checkSum)
				{
					Log(Foundation::ELogLevel::Warn,
						"Packet checksum mismatch. sessionId={} opcode={} expected={} actual={}",
						sessionContext.GetSessionId(),
						packetView.opcode,
						static_cast<int>(packetView.checkSum),
						static_cast<int>(actualChecksum));
					CloseSession(sessionContext);
					break;
				}

				if (m_packetCipher != nullptr && packetView.payloadLength > 0)
				{
					m_packetCipher->Decode(const_cast<char*>(packetView.payload), packetView.payloadLength, packetView.randomKey);
				}

				FPacketView contentPacketView;
				if (!TryParseContentPacketView(packetView, contentPacketView))
				{
					Log(Foundation::ELogLevel::Warn, "Content header parse failed. sessionId={}", sessionContext.GetSessionId());
					CloseSession(sessionContext);
					break;
				}

				m_applicationHandler->OnPacketReceived(*this, sessionContext.GetSessionId(), contentPacketView);
				m_monitoring.OnReceivePacket();

				const std::size_t consumedPacketSize = sizeof(SPacketHeader) + static_cast<std::size_t>(packetView.payloadLength);
				sessionContext.GetRecvBuffer().Discard(consumedPacketSize);
			}
		}
		else
		{
			Log(Foundation::ELogLevel::Warn, "Recv path without framer is not supported by RIO ring buffer mode.");
			CloseSession(sessionContext);
		}

		if (!sessionContext.IsClosing() && !PostRecv(sessionContext))
		{
			Log(Foundation::ELogLevel::Warn, "PostRecv failed after RIO packet dispatch. sessionId={}", sessionContext.GetSessionId());
			CloseSession(sessionContext);
		}
	}

	void FRioServer::HandleSendCompletion(
		FRioSession& sessionContext,
		const RIORESULT& completionResult)
	{
		if (completionResult.Status != ERROR_SUCCESS || completionResult.BytesTransferred == 0)
		{
			if (completionResult.Status != ERROR_SUCCESS)
			{
				Log(Foundation::ELogLevel::Warn,
					"RIO send completion failed. sessionId={} status={}",
					sessionContext.GetSessionId(),
					completionResult.Status);
			}
			CloseSession(sessionContext);
		}

		if (m_serverConfig.rioSendDispatchMode == ERioSendDispatchMode::Direct)
		{
			const auto waitStart = FSteadyClock::now();
			std::unique_lock<std::mutex> sendRingLock(sessionContext.GetSendRingMutex());
			const auto lockAcquired = FSteadyClock::now();
			m_monitoring.GetRioSendMetrics().RecordSendRingTouch(sessionContext);
			sessionContext.CompleteCurrentSend();
			const auto lockReleased = FSteadyClock::now();
			sendRingLock.unlock();
			m_monitoring.GetRioSendMetrics().RecordDirectSendRingLockSample(
				ToNanoseconds(lockAcquired - waitStart), ToNanoseconds(lockReleased - lockAcquired));
		}
		else
		{
			m_monitoring.GetRioSendMetrics().RecordSendRingTouch(sessionContext);
			sessionContext.CompleteCurrentSend();
		}

		if (!sessionContext.IsClosing() && m_serverConfig.rioSendDispatchMode == ERioSendDispatchMode::OwnerThread)
		{
			DrainOwnerThreadSendQueue(sessionContext);
		}
		else if (!sessionContext.IsClosing())
		{
			PostSend(sessionContext, sessionContext.GetSessionId());
		}
	}

	void FRioServer::CloseSession(
		FRioSession& sessionContext)
	{
		if (!sessionContext.TryMarkClosing())
		{
			return;
		}

		{
			std::scoped_lock<std::mutex> requestQueueLock(sessionContext.GetRequestQueueMutex());
			if (sessionContext.GetSocket() != INVALID_SOCKET)
			{
				shutdown(sessionContext.GetSocket(), SD_BOTH);
				closesocket(sessionContext.GetSocket());
				sessionContext.SetSocket(INVALID_SOCKET);
			}
			sessionContext.SetRequestQueue(RIO_INVALID_RQ);
		}

		m_sessionSlots[sessionContext.GetSlotIndex()].store(nullptr);
		m_monitoring.OnSessionClosed();
		if (sessionContext.GetOwnerWorkerIndex() < m_workers.size())
		{
			m_workers[sessionContext.GetOwnerWorkerIndex()]->activeSessionCount.fetch_sub(1, std::memory_order_relaxed);
			if (m_workers[sessionContext.GetOwnerWorkerIndex()]->completionEvent != nullptr)
			{
				SetEvent(m_workers[sessionContext.GetOwnerWorkerIndex()]->completionEvent);
			}
		}
		{
			Log(Foundation::ELogLevel::Info,
				"RIO session closed. sessionId={} workerIndex={}",
				sessionContext.GetSessionId(),
				sessionContext.GetOwnerWorkerIndex());
		}
		m_applicationHandler->OnClientDisconnected(sessionContext.GetSessionId());
	}

	void FRioServer::ReleaseSession(
		FRioSession* sessionContext)
	{
		if (sessionContext == nullptr)
		{
			return;
		}

		if (sessionContext->ReleaseRef() == 0)
		{
			sessionContext->ReleaseRioResources(m_rioFunctionTable);
			FRioSession::Destroy(sessionContext);
		}
	}

	FRioSession* FRioServer::AcquireSession(
		std::uint64_t sessionId)
	{
		const std::uint32_t slotIndex = static_cast<std::uint32_t>(sessionId & 0xFFFFFFFFULL);
		if (slotIndex >= m_serverConfig.maxSessionCount)
		{
			return nullptr;
		}

		FRioSession* sessionContext = m_sessionSlots[slotIndex].load();
		if (sessionContext == nullptr || sessionContext->GetSessionId() != sessionId || sessionContext->IsClosing())
		{
			return nullptr;
		}

		sessionContext->AcquireRef();
		if (sessionContext->GetSessionId() != sessionId || sessionContext->IsClosing())
		{
			ReleaseSession(sessionContext);
			return nullptr;
		}

		return sessionContext;
	}

	std::uint32_t FRioServer::ChooseLeastLoadedWorkerIndex() const noexcept
	{
		if (m_workers.empty())
		{
			return 0;
		}

		std::uint32_t selectedWorkerIndex = 0;
		std::uint32_t selectedLoad = m_workers[0]->activeSessionCount.load(std::memory_order_relaxed);
		for (std::uint32_t workerIndex = 1; workerIndex < m_workers.size(); ++workerIndex)
		{
			const std::uint32_t currentLoad = m_workers[workerIndex]->activeSessionCount.load(std::memory_order_relaxed);
			if (currentLoad < selectedLoad)
			{
				selectedLoad = currentLoad;
				selectedWorkerIndex = workerIndex;
			}
		}

		return selectedWorkerIndex;
	}

	std::uint64_t FRioServer::ComposeSessionId(
		std::uint32_t slotIndex,
		std::uint32_t generation) const noexcept
	{
		return (static_cast<std::uint64_t>(generation) << 32ULL) | static_cast<std::uint64_t>(slotIndex);
	}

	std::uint8_t FRioServer::GeneratePacketRandomKey() noexcept
	{
		return static_cast<std::uint8_t>(m_packetRandomKeySeed.fetch_add(1, std::memory_order_relaxed) & 0xFF);
	}

	void FRioServer::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "NetworkLib", message);
		}
	}
}
