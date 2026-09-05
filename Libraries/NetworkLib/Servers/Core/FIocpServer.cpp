#include "NetLibPch.h"

#include "Crypto/IPacketCipher.h"
#include "Packet/Buffer/FPacketBuffer.h"
#include "Packet/Serialization/FPacketSerialization.h"
#include "Packet/Framing/IPacketFramer.h"
#include "Servers/Core/FIocpServer.h"
#include "Servers/IApplicationHandler.h"
#include "Servers/Session/FIocpSession.h"
#include "Foundation/Logging/ILogger.h"

#include <format>
#pragma comment(lib, "Ws2_32.lib")

namespace NetworkLib::Core
{
	using NetworkLib::Packet::Buffer::FPacketBuffer;
	using NetworkLib::Packet::Buffer::FSendBuffer;
	using NetworkLib::Packet::Framing::CalculatePacketChecksum;
	using NetworkLib::Packet::Framing::SFramedPacketBufferParts;
	using NetworkLib::Packet::Framing::SOutgoingPacket;
	using NetworkLib::Packet::Framing::SPacketHeader;
	using NetworkLib::Packet::Serialization::TryParseContentPacketView;
	using NetworkLib::Packet::View::FPacketView;
	using NetworkLib::Session::FIocpSession;

	namespace
	{
		bool ApplyAcceptedSocketSendBufferOption(
			const SServerConfig& serverConfig,
			const SOCKET clientSocket,
			std::string& outError) noexcept
		{
			if (serverConfig.socketSendBufferBytes < 0)
			{
				return true;
			}

			const int sendBufferBytes = serverConfig.socketSendBufferBytes;
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

	FIocpServer::FIocpServer() = default;

	FIocpServer::~FIocpServer()
	{
		Stop();
	}

	bool FIocpServer::Start(
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
		FSendBuffer::ConfigurePageReuse(m_serverConfig.enablePageBufferReuse, m_serverConfig.pageBufferSize);
		if (!FSendBuffer::InitializeSegmentPool(false, nullptr, m_serverConfig.maxSessionCount))
		{
			Log(Foundation::ELogLevel::Error, "Send segment pool initialization failed.");
			m_isRunning = false;
			return false;
		}
		FPacketBuffer::ConfigurePageReuse(m_serverConfig.enablePageBufferReuse, m_serverConfig.pageBufferSize);
		if (m_packetCipher != nullptr && m_packetFramer == nullptr)
		{
			Log(Foundation::ELogLevel::Error, "Packet cipher requires packet framer.");
			m_isRunning = false;
			return false;
		}

		m_sessionSlots = std::make_unique<std::atomic<FIocpSession*>[]>(m_serverConfig.maxSessionCount);
		m_generations = std::make_unique<std::atomic<std::uint32_t>[]>(m_serverConfig.maxSessionCount);
		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			m_sessionSlots[slotIndex].store(nullptr);
			m_generations[slotIndex].store(1);
		}

		if (!InitializeWinsock())
		{
			Log(Foundation::ELogLevel::Error, "Winsock initialization failed.");
			Stop();
			return false;
		}

		m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		if (m_iocpHandle == nullptr)
		{
			Log(Foundation::ELogLevel::Error,

				"CreateIoCompletionPort failed. error={}",
				GetLastError());
			Stop();
			return false;
		}

		if (!OpenListenSocket())
		{
			Log(Foundation::ELogLevel::Error, "Listen socket open failed.");
			Stop();
			return false;
		}

		if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_listenSocket), m_iocpHandle, kAcceptCompletionKey, 0) == nullptr)
		{
			Log(Foundation::ELogLevel::Error, "CreateIoCompletionPort listen attach failed. error={}", GetLastError());
			Stop();
			return false;
		}

		if (!LoadAcceptExFunctions())
		{
			Log(Foundation::ELogLevel::Error, "AcceptEx extension function load failed.");
			Stop();
			return false;
		}

		if (!InitializeAcceptContexts())
		{
			Log(Foundation::ELogLevel::Error, "AcceptEx context initialization failed.");
			Stop();
			return false;
		}

		StartWorkers();
		const std::uint32_t workerCount = std::max(1u, m_serverConfig.workerThreadCount);
		{
			Log(Foundation::ELogLevel::Info,
				"Server started. ip={} port={} workers={} maxSessions={} acceptContexts={}",
				m_serverConfig.bindIp,
				m_serverConfig.port,
				workerCount,
				m_serverConfig.maxSessionCount,
				m_acceptContextCount);
		}
		m_applicationHandler->OnServerStarted(*this);
		for (std::uint32_t acceptSlotIndex = 0; acceptSlotIndex < m_acceptContextCount; ++acceptSlotIndex)
		{
			if (!PostAccept(acceptSlotIndex))
			{
				Log(Foundation::ELogLevel::Error, "Initial AcceptEx post failed. slot={}", acceptSlotIndex);
				Stop();
				return false;
			}
		}
		return true;
	}

	void FIocpServer::Stop()
	{
		if (!m_isRunning.load(std::memory_order_acquire))
		{
			return;
		}

		{
			std::lock_guard lifecycleLock(m_sessionLifecycleMutex);
			if (!m_isRunning.exchange(false, std::memory_order_acq_rel))
			{
				return;
			}

			Log(Foundation::ELogLevel::Info, "Server stop requested.");

			CloseListenSocket();

			for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
			{
				FIocpSession* sessionContext = m_sessionSlots[slotIndex].exchange(nullptr);
				if (sessionContext != nullptr)
				{
					CloseSession(*sessionContext);
					ReleaseSession(sessionContext);
				}
			}
		}

		StopWorkers();
		CloseAcceptContexts();
		FSendBuffer::ShutdownSegmentPool(nullptr);

		if (m_iocpHandle != nullptr)
		{
			CloseHandle(m_iocpHandle);
			m_iocpHandle = nullptr;
		}

		if (m_winsockInitialized.exchange(false))
		{
			WSACleanup();
		}

		if (m_applicationHandler != nullptr)
		{
			m_applicationHandler->OnServerStopped();
			m_applicationHandler = nullptr;
		}

		Log(Foundation::ELogLevel::Info, "Server stopped.");
		m_packetCipher.reset();
		m_packetFramer.reset();
		m_logger.reset();
	}

	bool FIocpServer::SendPacket(
		std::uint64_t sessionId,
		NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet)
	{
		if (!packet.IsValid())
		{
			Log(Foundation::ELogLevel::Warn, "Send rejected because outgoing packet was invalid.");
			return false;
		}

		const std::int32_t bodyLength = packet.GetBodyLength();
		FIocpSession* sessionContext = AcquireSession(sessionId);
		if (sessionContext == nullptr)
		{
			Log(Foundation::ELogLevel::Warn, "Send rejected because session was not found. sessionId={}", sessionId);
			return false;
		}

		if (m_packetFramer != nullptr)
		{
			std::vector<char> payloadBuffer = packet.MoveBuffer();
			std::uint8_t randomKey = 0;
			if (m_packetCipher != nullptr)
			{
				randomKey = GeneratePacketRandomKey();
				m_packetCipher->Encode(payloadBuffer.data(), static_cast<int>(payloadBuffer.size()), randomKey);
			}

			SOutgoingPacket outgoingPacket{};
			outgoingPacket.randomKey = randomKey;
			outgoingPacket.checkSum = CalculatePacketChecksum(payloadBuffer.data(), static_cast<std::int32_t>(payloadBuffer.size()));
			outgoingPacket.payload = payloadBuffer.data();
			outgoingPacket.payloadLength = static_cast<std::int32_t>(payloadBuffer.size());

			SFramedPacketBufferParts packetParts{};
			if (!m_packetFramer->BuildPacketParts(outgoingPacket, packetParts))
			{
				Log(Foundation::ELogLevel::Error, "BuildPacketParts failed during send path.");
				ReleaseSession(sessionContext);
				return false;
			}

			sessionContext->EnqueueSendBuffer(FSendBuffer::Create(packetParts, std::move(payloadBuffer)));
		}
		else
		{
			std::vector<char> payloadBuffer = packet.MoveBuffer();
			sessionContext->EnqueueSendBuffer(FSendBuffer::Create(std::move(payloadBuffer)));
		}
		m_monitoring.OnSendPacket(static_cast<std::uint64_t>(bodyLength > 0 ? bodyLength : 0));
		PostSend(*sessionContext);

		ReleaseSession(sessionContext);
		return true;
	}

	bool FIocpServer::Disconnect(
		std::uint64_t sessionId)
	{
		FIocpSession* sessionContext = AcquireSession(sessionId);
		if (sessionContext == nullptr)
		{
			return false;
		}

		CloseSession(*sessionContext);
		ReleaseSession(sessionContext);
		return true;
	}

	EBackendKind FIocpServer::GetBackendKind() const
	{
		return EBackendKind::Iocp;
	}

	SServerStats FIocpServer::GetStatsSnapshot() const
	{
		NetworkLib::Diagnostics::SServerMonitoringSnapshotInput snapshotInput{};
		snapshotInput.pools.sessionPoolCapacity = static_cast<std::uint32_t>(FIocpSession::GetPoolCapacity());
		snapshotInput.pools.sessionPoolUsage = static_cast<std::uint32_t>(FIocpSession::GetPoolUsage());
		snapshotInput.pools.sendBufferPoolCapacity = static_cast<std::uint32_t>(FSendBuffer::GetPoolCapacity());
		snapshotInput.pools.sendBufferPoolUsage = static_cast<std::uint32_t>(FSendBuffer::GetPoolUsage());
		snapshotInput.pools.packetBufferPoolCapacity = static_cast<std::uint32_t>(FPacketBuffer::GetPoolCapacity());
		snapshotInput.pools.packetBufferPoolUsage = static_cast<std::uint32_t>(FPacketBuffer::GetPoolUsage());

		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			FIocpSession* sessionContext = AcquireSessionBySlotIndex(slotIndex);
			if (sessionContext == nullptr)
			{
				continue;
			}

			snapshotInput.session.queuedSendBufferCount += sessionContext->GetQueuedSendBufferCount();
			snapshotInput.session.maxObservedQueuedSendBufferCount = std::max<std::uint64_t>(
				snapshotInput.session.maxObservedQueuedSendBufferCount, sessionContext->GetMaxObservedQueuedSendBufferCount());
			ReleaseSession(sessionContext);
		}
		return m_monitoring.BuildSnapshot(snapshotInput);
	}

	bool FIocpServer::InitializeWinsock()
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

	bool FIocpServer::OpenListenSocket()
	{
		m_listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
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

	bool FIocpServer::LoadAcceptExFunctions()
	{
		DWORD bytesReturned = 0;
		GUID acceptExGuid = WSAID_ACCEPTEX;
		if (WSAIoctl(m_listenSocket,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&acceptExGuid,
				sizeof(acceptExGuid),
				&m_acceptEx,
				sizeof(m_acceptEx),
				&bytesReturned,
				nullptr,
				nullptr) == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "WSAIoctl(AcceptEx) failed. error={}", WSAGetLastError());
			return false;
		}

		GUID getAcceptExSockaddrsGuid = WSAID_GETACCEPTEXSOCKADDRS;
		if (WSAIoctl(m_listenSocket,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&getAcceptExSockaddrsGuid,
				sizeof(getAcceptExSockaddrsGuid),
				&m_getAcceptExSockaddrs,
				sizeof(m_getAcceptExSockaddrs),
				&bytesReturned,
				nullptr,
				nullptr) == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Error, "WSAIoctl(GetAcceptExSockaddrs) failed. error={}", WSAGetLastError());
			return false;
		}

		return true;
	}

	bool FIocpServer::InitializeAcceptContexts()
	{
		if (m_serverConfig.maxSessionCount == 0)
		{
			Log(Foundation::ELogLevel::Error, "AcceptEx initialization requires maxSessionCount > 0.");
			return false;
		}

		const std::uint32_t desiredAcceptContextCount =
			std::max(kMinimumAcceptContextCount, std::max(1u, m_serverConfig.workerThreadCount) * 2u);
		m_acceptContextCount = std::min(m_serverConfig.maxSessionCount, desiredAcceptContextCount);
		m_acceptContexts = std::make_unique<SAcceptContext[]>(m_acceptContextCount);
		for (std::uint32_t acceptSlotIndex = 0; acceptSlotIndex < m_acceptContextCount; ++acceptSlotIndex)
		{
			m_acceptContexts[acceptSlotIndex].slotIndex = acceptSlotIndex;
			m_acceptContexts[acceptSlotIndex].acceptedSocket = INVALID_SOCKET;
			m_acceptContexts[acceptSlotIndex].ResetOverlapped();
		}

		return true;
	}

	bool FIocpServer::PostAccept(
		std::uint32_t acceptSlotIndex)
	{
		if (acceptSlotIndex >= m_acceptContextCount || m_acceptEx == nullptr)
		{
			return false;
		}

		SAcceptContext& acceptContext = m_acceptContexts[acceptSlotIndex];
		if (acceptContext.acceptedSocket != INVALID_SOCKET)
		{
			closesocket(acceptContext.acceptedSocket);
			acceptContext.acceptedSocket = INVALID_SOCKET;
		}

		acceptContext.acceptedSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (acceptContext.acceptedSocket == INVALID_SOCKET)
		{
			Log(Foundation::ELogLevel::Error, "WSASocketW for AcceptEx failed. slot={} error={}", acceptSlotIndex, WSAGetLastError());
			return false;
		}

		acceptContext.ResetOverlapped();

		DWORD bytesReceived = 0;
		const BOOL acceptResult = m_acceptEx(m_listenSocket,
			acceptContext.acceptedSocket,
			acceptContext.buffer.data(),
			0,
			static_cast<DWORD>((sizeof(sockaddr_in) + 16)),
			static_cast<DWORD>((sizeof(sockaddr_in) + 16)),
			&bytesReceived,
			&acceptContext.overlapped);
		if (acceptResult == FALSE)
		{
			const int errorCode = WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				Log(Foundation::ELogLevel::Error, "AcceptEx post failed. slot={} error={}", acceptSlotIndex, errorCode);
				closesocket(acceptContext.acceptedSocket);
				acceptContext.acceptedSocket = INVALID_SOCKET;
				return false;
			}
		}

		return true;
	}

	void FIocpServer::CloseAcceptContexts() noexcept
	{
		if (m_acceptContexts == nullptr)
		{
			return;
		}

		for (std::uint32_t acceptSlotIndex = 0; acceptSlotIndex < m_acceptContextCount; ++acceptSlotIndex)
		{
			SAcceptContext& acceptContext = m_acceptContexts[acceptSlotIndex];
			if (acceptContext.acceptedSocket != INVALID_SOCKET)
			{
				closesocket(acceptContext.acceptedSocket);
				acceptContext.acceptedSocket = INVALID_SOCKET;
			}
		}

		m_acceptContexts.reset();
		m_acceptContextCount = 0;
	}

	void FIocpServer::CloseListenSocket()
	{
		if (m_listenSocket != INVALID_SOCKET)
		{
			closesocket(m_listenSocket);
			m_listenSocket = INVALID_SOCKET;
		}
	}

	void FIocpServer::StartWorkers()
	{
		const std::uint32_t workerCount = std::max(1u, m_serverConfig.workerThreadCount);
		m_workerThreads.reserve(workerCount);
		for (std::uint32_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			m_workerThreads.emplace_back(&FIocpServer::WorkerLoop, this);
		}
	}

	void FIocpServer::StopWorkers()
	{
		for (std::size_t workerIndex = 0; workerIndex < m_workerThreads.size(); ++workerIndex)
		{
			PostQueuedCompletionStatus(m_iocpHandle, 0, 0, nullptr);
		}

		for (auto& workerThread : m_workerThreads)
		{
			if (workerThread.joinable())
			{
				workerThread.join();
			}
		}

		m_workerThreads.clear();
	}

	bool FIocpServer::HandleAcceptCompletion(
		SAcceptContext& acceptContext,
		bool completionSucceeded,
		DWORD completionError)
	{
		if (acceptContext.acceptedSocket == INVALID_SOCKET)
		{
			return false;
		}

		if (!completionSucceeded)
		{
			if (m_isRunning && completionError != ERROR_OPERATION_ABORTED)
			{
				Log(Foundation::ELogLevel::Warn, "AcceptEx completion failed. slot={} error={}", acceptContext.slotIndex, completionError);
			}

			closesocket(acceptContext.acceptedSocket);
			acceptContext.acceptedSocket = INVALID_SOCKET;
			return false;
		}

		if (!m_isRunning)
		{
			closesocket(acceptContext.acceptedSocket);
			acceptContext.acceptedSocket = INVALID_SOCKET;
			return false;
		}

		if (!AttachAcceptedSocket(acceptContext.acceptedSocket))
		{
			closesocket(acceptContext.acceptedSocket);
			acceptContext.acceptedSocket = INVALID_SOCKET;
			return false;
		}

		acceptContext.acceptedSocket = INVALID_SOCKET;
		return true;
	}

	void FIocpServer::WorkerLoop()
	{
		while (true)
		{
			DWORD transferredBytes = 0;
			ULONG_PTR completionKey = 0;
			LPOVERLAPPED overlapped = nullptr;

			const BOOL queuedResult = GetQueuedCompletionStatus(m_iocpHandle, &transferredBytes, &completionKey, &overlapped, INFINITE);
			const DWORD completionError = queuedResult != FALSE ? ERROR_SUCCESS : GetLastError();
			if (overlapped == nullptr && completionKey == 0)
			{
				break;
			}

			if (completionKey == kAcceptCompletionKey)
			{
				auto* acceptContext = reinterpret_cast<SAcceptContext*>(overlapped);
				if (acceptContext == nullptr)
				{
					continue;
				}

				HandleAcceptCompletion(*acceptContext, queuedResult != FALSE, completionError);
				if (m_isRunning && !PostAccept(acceptContext->slotIndex))
				{
					Log(Foundation::ELogLevel::Error, "AcceptEx repost failed. slot={}", acceptContext->slotIndex);
				}

				continue;
			}

			auto* ioContext = reinterpret_cast<FIocpSession::SIoContext*>(overlapped);
			FIocpSession* sessionContext = ioContext->ownerSession;
			if (sessionContext == nullptr)
			{
				continue;
			}

			if (queuedResult == FALSE || transferredBytes == 0)
			{
				if (queuedResult == FALSE)
				{
					Log(Foundation::ELogLevel::Warn,
						"I/O completion failed. sessionId={} error={}",
						sessionContext->GetSessionId(),
						completionError);
				}
				if (ioContext->ioType == FIocpSession::EIoType::Send)
				{
					sessionContext->FinishSendIo();
					sessionContext->ReleaseActiveSendBuffers();
					sessionContext->EndSend();
				}
				CloseSession(*sessionContext);
				ReleaseSession(sessionContext);
				continue;
			}

			if (ioContext->ioType == FIocpSession::EIoType::Recv)
			{
				m_monitoring.OnReceiveBytes(transferredBytes);
				if (!sessionContext->CommitRecvBytes(transferredBytes))
				{
					Log(Foundation::ELogLevel::Warn, "Recv buffer overflow detected.");
					CloseSession(*sessionContext);
					ReleaseSession(sessionContext);
					continue;
				}

				if (m_packetFramer != nullptr)
				{
					while (true)
					{
						FPacketView packetView;
						if (!m_packetFramer->TryExtractPacketView(sessionContext->GetRecvBuffer(), packetView))
						{
							if (m_packetFramer->HasInvalidPacketHeader(sessionContext->GetRecvBuffer()))
							{
								Log(Foundation::ELogLevel::Warn,
									"Oversized packet header rejected. sessionId={}",
									sessionContext->GetSessionId());
								CloseSession(*sessionContext);
							}
							break;
						}

						const std::uint8_t actualChecksum = CalculatePacketChecksum(packetView.payload, packetView.payloadLength);
						if (actualChecksum != packetView.checkSum)
						{
							Log(Foundation::ELogLevel::Warn,
								"Packet checksum mismatch. sessionId={} opcode={} expected={} actual={}",
								sessionContext->GetSessionId(),
								packetView.opcode,
								static_cast<int>(packetView.checkSum),
								static_cast<int>(actualChecksum));
							CloseSession(*sessionContext);
							break;
						}

						if (m_packetCipher != nullptr && packetView.payloadLength > 0)
						{
							m_packetCipher->Decode(const_cast<char*>(packetView.payload), packetView.payloadLength, packetView.randomKey);
						}

						FPacketView contentPacketView;
						if (!TryParseContentPacketView(packetView, contentPacketView))
						{
							Log(Foundation::ELogLevel::Warn, "Content header parse failed. sessionId={}", sessionContext->GetSessionId());
							CloseSession(*sessionContext);
							break;
						}

						m_applicationHandler->OnPacketReceived(*this, sessionContext->GetSessionId(), contentPacketView);
						m_monitoring.OnReceivePacket();

						const std::size_t consumedPacketSize = sizeof(SPacketHeader) + static_cast<std::size_t>(packetView.payloadLength);
						sessionContext->GetRecvBuffer().Discard(consumedPacketSize);
					}
				}
				else
				{
					Log(Foundation::ELogLevel::Warn, "Recv path without framer is not supported by ring buffer mode.");
					CloseSession(*sessionContext);
				}

				if (!PostRecv(*sessionContext))
				{
					Log(Foundation::ELogLevel::Warn, "PostRecv failed after packet dispatch. sessionId={}", sessionContext->GetSessionId());
					CloseSession(*sessionContext);
				}
			}
			else
			{
				sessionContext->FinishSendIo();
				sessionContext->ReleaseActiveSendBuffers();
				const bool sendRestartRequested = sessionContext->EndSend();
				if (!sessionContext->IsClosing())
				{
					if (sendRestartRequested || sessionContext->GetQueuedSendBufferCount() > 0)
					{
						PostSend(*sessionContext);
					}
				}
			}

			ReleaseSession(sessionContext);
		}
	}

	bool FIocpServer::PostRecv(
		FIocpSession& sessionContext)
	{
		DWORD recvFlags = 0;
		DWORD recvBytes = 0;
		WSABUF recvBuffers[2]{};
		DWORD recvBufferCount = 0;

		FIocpSession::SIoContext& recvContext = sessionContext.GetRecvContext();
		recvContext.Prepare(FIocpSession::EIoType::Recv, &sessionContext);
		sessionContext.BuildRecvWsabufs(recvBuffers, recvBufferCount);
		if (recvBufferCount == 0)
		{
			Log(Foundation::ELogLevel::Warn, "PostRecv failed because recv buffer has no writable space.");
			return false;
		}
		sessionContext.AcquireRef();
		m_monitoring.OnWsaRecvCall();

		const int recvResult =
			WSARecv(sessionContext.GetSocket(), recvBuffers, recvBufferCount, &recvBytes, &recvFlags, &recvContext.overlapped, nullptr);
		if (recvResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
		{
			const int errorCode = WSAGetLastError();
			Log(Foundation::ELogLevel::Warn, "WSARecv failed. sessionId={} error={}", sessionContext.GetSessionId(), errorCode);
			ReleaseSession(&sessionContext);
			return false;
		}

		return true;
	}

	bool FIocpServer::PostSend(
		FIocpSession& sessionContext)
	{
		while (true)
		{
			if (!sessionContext.TryBeginSend())
			{
				return false;
			}

			if (sessionContext.IsClosing())
			{
				const bool sendRestartRequested = sessionContext.EndSend();
				if (sendRestartRequested)
				{
					continue;
				}
				return false;
			}

			if (!sessionContext.FillSendBatch(kMaxSendBatchCount))
			{
				const bool sendRestartRequested = sessionContext.EndSend();
				if (sendRestartRequested)
				{
					continue;
				}
				return false;
			}

			FIocpSession::SIoContext& sendContext = sessionContext.GetSendContext();
			sendContext.Prepare(FIocpSession::EIoType::Send, &sessionContext);

			sessionContext.AcquireRef();
			const int concurrentSendIoCount = sessionContext.BeginSendIo();
			if (concurrentSendIoCount > 1)
			{
				Log(Foundation::ELogLevel::Error,
					"Concurrent WSASend detected. sessionId={} concurrentSendIoCount={}",
					sessionContext.GetSessionId(),
					concurrentSendIoCount);
				sessionContext.FinishSendIo();
				sessionContext.ReleaseActiveSendBuffers();
				sessionContext.EndSend();
				ReleaseSession(&sessionContext);
				CloseSession(sessionContext);
				return false;
			}

			DWORD sentBytes = 0;
			DWORD sendFlags = 0;
			const std::vector<WSABUF>& sendBuffers = sessionContext.GetSendWsabufs();
			m_monitoring.OnWsaSendCall();
			const int sendResult = WSASend(sessionContext.GetSocket(),
				const_cast<WSABUF*>(sendBuffers.data()),
				static_cast<DWORD>(sendBuffers.size()),
				&sentBytes,
				sendFlags,
				&sendContext.overlapped,
				nullptr);

			if (sendResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
			{
				const int errorCode = WSAGetLastError();
				Log(Foundation::ELogLevel::Error, "WSASend failed. sessionId={} error={}", sessionContext.GetSessionId(), errorCode);
				sessionContext.FinishSendIo();
				sessionContext.ReleaseActiveSendBuffers();
				sessionContext.EndSend();
				ReleaseSession(&sessionContext);
				CloseSession(sessionContext);
				return false;
			}

			return true;
		}
	}

	void FIocpServer::CloseSession(
		FIocpSession& sessionContext)
	{
		if (!sessionContext.TryMarkClosing())
		{
			return;
		}
		sessionContext.AcquireRef();

		shutdown(sessionContext.GetSocket(), SD_BOTH);
		closesocket(sessionContext.GetSocket());
		sessionContext.SetSocket(INVALID_SOCKET);
		FIocpSession* expectedSession = &sessionContext;
		const bool releasedSlotReference = m_sessionSlots[sessionContext.GetSlotIndex()].compare_exchange_strong(expectedSession, nullptr);
		m_monitoring.OnSessionClosed();
		{
			Log(Foundation::ELogLevel::Info,

				"Session closed. sessionId={} maxConcurrentSendIo={}",
				sessionContext.GetSessionId(),
				sessionContext.GetMaxObservedConcurrentSendIoCount());
		}
		m_applicationHandler->OnClientDisconnected(sessionContext.GetSessionId());
		if (releasedSlotReference)
		{
			// Return the initial reference owned by the session slot.
			ReleaseSession(&sessionContext);
		}
		// Return the temporary reference that protects this close callback path.
		ReleaseSession(&sessionContext);
	}

	void FIocpServer::ReleaseSession(
		FIocpSession* sessionContext) const
	{
		if (sessionContext == nullptr)
		{
			return;
		}

		if (sessionContext->ReleaseRef() == 0)
		{
			FIocpSession::Destroy(sessionContext);
		}
	}

	FIocpSession* FIocpServer::AcquireSessionBySlotIndex(
		std::uint32_t slotIndex) const
	{
		if (slotIndex >= m_serverConfig.maxSessionCount)
		{
			return nullptr;
		}

		FIocpSession* sessionContext = m_sessionSlots[slotIndex].load(std::memory_order_acquire);
		if (sessionContext == nullptr || !sessionContext->TryAcquireRef())
		{
			return nullptr;
		}

		if (m_sessionSlots[slotIndex].load(std::memory_order_acquire) != sessionContext || sessionContext->IsClosing())
		{
			ReleaseSession(sessionContext);
			return nullptr;
		}

		return sessionContext;
	}

	FIocpSession* FIocpServer::AcquireSession(
		std::uint64_t sessionId)
	{
		const std::uint32_t slotIndex = static_cast<std::uint32_t>(sessionId & 0xFFFFFFFFULL);
		if (slotIndex >= m_serverConfig.maxSessionCount)
		{
			return nullptr;
		}

		FIocpSession* sessionContext = AcquireSessionBySlotIndex(slotIndex);
		if (sessionContext == nullptr)
		{
			return nullptr;
		}

		if (sessionContext->GetSessionId() != sessionId)
		{
			ReleaseSession(sessionContext);
			return nullptr;
		}

		return sessionContext;
	}

	bool FIocpServer::AttachAcceptedSocket(
		SOCKET clientSocket)
	{
		std::lock_guard lifecycleLock(m_sessionLifecycleMutex);
		if (!m_isRunning.load(std::memory_order_acquire))
		{
			return false;
		}

		if (setsockopt(clientSocket,
				SOL_SOCKET,
				SO_UPDATE_ACCEPT_CONTEXT,
				reinterpret_cast<const char*>(&m_listenSocket),
				sizeof(m_listenSocket)) == SOCKET_ERROR)
		{
			Log(Foundation::ELogLevel::Warn, "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed. error={}", WSAGetLastError());
			return false;
		}

		{
			std::string errorMessage;
			if (!ApplyAcceptedSocketSendBufferOption(m_serverConfig, clientSocket, errorMessage))
			{
				Log(Foundation::ELogLevel::Error, errorMessage);
				return false;
			}
		}

		if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), m_iocpHandle, 0, 0) == nullptr)
		{
			Log(Foundation::ELogLevel::Error, "CreateIoCompletionPort attach failed. error={}", GetLastError());
			return false;
		}

		FIocpSession* newSessionContext = nullptr;

		for (std::uint32_t slotIndex = 0; slotIndex < m_serverConfig.maxSessionCount; ++slotIndex)
		{
			FIocpSession* expected = nullptr;
			FIocpSession* candidateSession = FIocpSession::Create();
			const std::uint32_t generation = m_generations[slotIndex].fetch_add(1);
			const std::uint64_t sessionId = ComposeSessionId(slotIndex, generation);
			const std::size_t recvBufferCapacity =
				static_cast<std::size_t>(std::max<std::uint32_t>(m_serverConfig.recvBufferSize * 8u, 65536u));
			candidateSession->Initialize(clientSocket, sessionId, slotIndex, generation, recvBufferCapacity);

			if (m_sessionSlots[slotIndex].compare_exchange_strong(expected, candidateSession))
			{
				newSessionContext = candidateSession;
				break;
			}

			ReleaseSession(candidateSession);
		}

		if (newSessionContext == nullptr)
		{
			Log(Foundation::ELogLevel::Warn, "All session slots are in use.");
			return false;
		}

		{
			Log(Foundation::ELogLevel::Info, "Client connected. sessionId={}", newSessionContext->GetSessionId());
		}
		m_monitoring.OnSessionAccepted();
		newSessionContext->AcquireRef();
		m_applicationHandler->OnClientConnected(newSessionContext->GetSessionId());
		const bool recvPosted = PostRecv(*newSessionContext);
		if (!recvPosted)
		{
			CloseSession(*newSessionContext);
		}
		ReleaseSession(newSessionContext);

		return recvPosted;
	}

	std::uint64_t FIocpServer::ComposeSessionId(
		std::uint32_t slotIndex,
		std::uint32_t generation) const
	{
		return (static_cast<std::uint64_t>(generation) << 32ULL) | static_cast<std::uint64_t>(slotIndex);
	}

	std::uint8_t FIocpServer::GeneratePacketRandomKey() noexcept
	{
		return static_cast<std::uint8_t>(m_packetRandomKeySeed.fetch_add(1, std::memory_order_relaxed) & 0xFF);
	}

	void FIocpServer::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "NetworkLib", message);
		}
	}
}
