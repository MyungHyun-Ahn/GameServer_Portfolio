#include "ClientNetPch.h"

#include "FClientNetwork.h"
#include "NetworkLib/Crypto/FDefaultPacketCipher.h"
#include "NetworkLib/Packet/Framing/FDefaultPacketFramer.h"
#include "NetworkLib/Packet/Framing/PacketTypes.h"

namespace ClientNetworkLib
{
	namespace
	{
		constexpr ULONG_PTR kShutdownCompletionKey = 1;

		std::string BuildWinsockErrorMessage(
			const char* prefix,
			const int errorCode)
		{
			std::ostringstream oss;
			if (prefix != nullptr && *prefix != '\0')
			{
				oss << prefix;
			}
			else
			{
				oss << "winsock error";
			}

			oss << " error=" << errorCode;
			return oss.str();
		}

		bool TryBuildServerAddress(
			const std::string& serverIp,
			const std::uint16_t serverPort,
			sockaddr_in& outServerAddress,
			std::string& outErrorMessage)
		{
			if (serverIp.empty())
			{
				outErrorMessage = "server ip is empty.";
				return false;
			}

			if (serverPort == 0)
			{
				outErrorMessage = "server port must be greater than zero.";
				return false;
			}

			std::memset(&outServerAddress, 0, sizeof(outServerAddress));
			outServerAddress.sin_family = AF_INET;
			outServerAddress.sin_port = htons(serverPort);
			if (InetPtonA(AF_INET, serverIp.c_str(), &outServerAddress.sin_addr) != 1)
			{
				outErrorMessage = "invalid server ip address.";
				return false;
			}

			return true;
		}
	}

	struct FClientNetwork::FImpl
	{
		struct FClientSession final
		{
			enum class EIoType : std::uint8_t
			{
				Recv,
				Send
			};

			struct FIoContext final
			{
				OVERLAPPED Overlapped{};
				EIoType IoType = EIoType::Recv;
				FClientSession* OwnerSession = nullptr;

				void Prepare(
					const EIoType newIoType,
					FClientSession* newOwnerSession) noexcept
				{
					std::memset(&Overlapped, 0, sizeof(Overlapped));
					IoType = newIoType;
					OwnerSession = newOwnerSession;
				}
			};

			FClientSession(
				const SOCKET socketHandle,
				const FClientSessionId sessionId,
				const std::size_t recvScratchBufferSize)
				: SocketHandle(socketHandle)
				, SessionId(sessionId)
				, RecvScratchBuffer(std::max<std::size_t>(1, recvScratchBufferSize), 0)
			{
				InboundBuffer.reserve(RecvScratchBuffer.size() * 2);
				RecvContext.Prepare(EIoType::Recv, this);
				SendContext.Prepare(EIoType::Send, this);
			}

			~FClientSession()
			{
				CloseSocket();
			}

			long AcquireRef() noexcept
			{
				return ReferenceCount.fetch_add(1, std::memory_order_relaxed) + 1;
			}

			long ReleaseRef() noexcept
			{
				const long remainingRefCount = ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
				if (remainingRefCount == 0)
				{
					delete this;
				}

				return remainingRefCount;
			}

			bool TryMarkClosing() noexcept
			{
				bool expected = false;
				return Closing.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
			}

			bool IsClosing() const noexcept
			{
				return Closing.load(std::memory_order_acquire);
			}

			void CloseSocket() noexcept
			{
				const SOCKET socketHandle = std::exchange(SocketHandle, INVALID_SOCKET);
				if (socketHandle == INVALID_SOCKET)
				{
					return;
				}

				shutdown(socketHandle, SD_BOTH);
				closesocket(socketHandle);
			}

			std::atomic<long> ReferenceCount = 1;
			std::atomic<bool> Closing = false;
			SOCKET SocketHandle = INVALID_SOCKET;
			FClientSessionId SessionId = 0;
			std::mutex Mutex;
			FIoContext RecvContext{};
			FIoContext SendContext{};
			std::vector<char> RecvScratchBuffer;
			std::vector<char> InboundBuffer;
			std::deque<std::vector<char>> SendQueue;
			std::vector<char> ActiveSendBuffer;
			std::size_t ActiveSendOffset = 0;
			bool RecvPosted = false;
			bool SendInFlight = false;
		};

		explicit FImpl(
			const FClientNetworkConfig& config)
			: Config(
				  [&config]()
				  {
					  FClientNetworkConfig normalizedConfig = config;
					  normalizedConfig.WorkerThreadCount = std::max<std::uint32_t>(1, normalizedConfig.WorkerThreadCount);
					  normalizedConfig.RecvScratchBufferSize = std::max<std::size_t>(1, normalizedConfig.RecvScratchBufferSize);
					  return normalizedConfig;
				  }())
			, PacketCipher(Config.PacketCipherConfig)
		{
		}

		~FImpl()
		{
			Stop();
		}

		bool Start(
			std::string& outErrorMessage)
		{
			std::lock_guard<std::mutex> lifecycleLock(LifecycleMutex);
			if (Running.load(std::memory_order_acquire))
			{
				return true;
			}

			WSADATA wsaData{};
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			{
				outErrorMessage = "WSAStartup failed.";
				return false;
			}

			IocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
			if (IocpHandle == nullptr)
			{
				outErrorMessage = BuildWinsockErrorMessage("CreateIoCompletionPort failed.", static_cast<int>(GetLastError()));
				WSACleanup();
				return false;
			}

			Running.store(true, std::memory_order_release);
			try
			{
				WorkerThreads.reserve(Config.WorkerThreadCount);
				for (std::uint32_t index = 0; index < Config.WorkerThreadCount; ++index)
				{
					WorkerThreads.emplace_back(
						[this]()
						{
							WorkerLoop();
						});
				}
			}
			catch (const std::exception& exception)
			{
				outErrorMessage = exception.what();
				Running.store(false, std::memory_order_release);
				for (std::size_t index = 0; index < WorkerThreads.size(); ++index)
				{
					PostQueuedCompletionStatus(IocpHandle, 0, kShutdownCompletionKey, nullptr);
				}

				for (std::thread& workerThread : WorkerThreads)
				{
					if (workerThread.joinable())
					{
						workerThread.join();
					}
				}

				WorkerThreads.clear();
				CloseHandle(IocpHandle);
				IocpHandle = nullptr;
				WSACleanup();
				return false;
			}

			return true;
		}

		void Stop()
		{
			std::lock_guard<std::mutex> lifecycleLock(LifecycleMutex);
			if (!Running.load(std::memory_order_acquire) && IocpHandle == nullptr)
			{
				return;
			}

			Running.store(false, std::memory_order_release);

			std::vector<FClientSession*> sessionsToClose;
			{
				std::lock_guard<std::mutex> sessionLock(SessionMutex);
				sessionsToClose.reserve(Sessions.size());
				for (const auto& [sessionId, session] : Sessions)
				{
					(void)sessionId;
					sessionsToClose.push_back(session);
				}

				Sessions.clear();
			}

			for (FClientSession* session : sessionsToClose)
			{
				if (session == nullptr)
				{
					continue;
				}

				const bool firstClose = session->TryMarkClosing();
				session->CloseSocket();
				if (firstClose)
				{
					QueueEvent(BuildEvent(EClientEventType::Disconnected, session->SessionId, 0, "runtime stopped."));
				}

				session->ReleaseRef();
			}

			if (IocpHandle != nullptr)
			{
				for (std::size_t index = 0; index < WorkerThreads.size(); ++index)
				{
					PostQueuedCompletionStatus(IocpHandle, 0, kShutdownCompletionKey, nullptr);
				}
			}

			for (std::thread& workerThread : WorkerThreads)
			{
				if (workerThread.joinable())
				{
					workerThread.join();
				}
			}

			WorkerThreads.clear();

			if (IocpHandle != nullptr)
			{
				CloseHandle(IocpHandle);
				IocpHandle = nullptr;
			}

			WSACleanup();
		}

		bool IsRunning() const noexcept
		{
			return Running.load(std::memory_order_acquire);
		}

		bool ConnectSession(
			FClientSessionId& outSessionId,
			std::string& outErrorMessage)
		{
			outSessionId = 0;
			if (!Running.load(std::memory_order_acquire) || IocpHandle == nullptr)
			{
				outErrorMessage = "client network is not running.";
				return false;
			}

			sockaddr_in serverAddress{};
			if (!TryBuildServerAddress(Config.ServerIp, Config.ServerPort, serverAddress, outErrorMessage))
			{
				QueueEvent(BuildEvent(EClientEventType::ConnectFailed, 0, 0, outErrorMessage));
				return false;
			}

			SOCKET socketHandle = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
			if (socketHandle == INVALID_SOCKET)
			{
				const int errorCode = WSAGetLastError();
				outErrorMessage = BuildWinsockErrorMessage("WSASocket failed.", errorCode);
				QueueEvent(BuildEvent(EClientEventType::ConnectFailed, 0, errorCode, outErrorMessage));
				return false;
			}

			if (connect(socketHandle, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR)
			{
				const int errorCode = WSAGetLastError();
				outErrorMessage = BuildWinsockErrorMessage("connect failed.", errorCode);
				QueueEvent(BuildEvent(EClientEventType::ConnectFailed, 0, errorCode, outErrorMessage));
				closesocket(socketHandle);
				return false;
			}

			if (Config.DisableNagle)
			{
				BOOL noDelay = TRUE;
				setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
			}

			if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(socketHandle), IocpHandle, 0, 0) == nullptr)
			{
				const int errorCode = static_cast<int>(GetLastError());
				outErrorMessage = BuildWinsockErrorMessage("CreateIoCompletionPort attach failed.", errorCode);
				QueueEvent(BuildEvent(EClientEventType::ConnectFailed, 0, errorCode, outErrorMessage));
				closesocket(socketHandle);
				return false;
			}

			const FClientSessionId sessionId = NextSessionId.fetch_add(1, std::memory_order_relaxed);
			FClientSession* session = new FClientSession(socketHandle, sessionId, Config.RecvScratchBufferSize);
			{
				std::lock_guard<std::mutex> sessionLock(SessionMutex);
				Sessions.emplace(sessionId, session);
			}

			std::string postRecvErrorMessage;
			int postRecvErrorCode = 0;
			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				if (!PostRecvLocked(*session, postRecvErrorMessage, postRecvErrorCode))
				{
					RemoveSession(session);
					session->CloseSocket();
					session->ReleaseRef();
					outErrorMessage = postRecvErrorMessage;
					QueueEvent(BuildEvent(EClientEventType::ConnectFailed, sessionId, postRecvErrorCode, outErrorMessage));
					return false;
				}
			}

			outSessionId = sessionId;
			QueueEvent(BuildEvent(EClientEventType::Connected, sessionId, 0, "session connected."));
			return true;
		}

		bool DisconnectSession(
			const FClientSessionId sessionId,
			const std::string& reasonMessage)
		{
			FClientSession* session = AcquireSession(sessionId);
			if (session == nullptr)
			{
				return false;
			}

			const std::string message = reasonMessage.empty() ? std::string("session disconnected by client.") : reasonMessage;
			CloseSession(session, EClientEventType::Disconnected, 0, message);
			session->ReleaseRef();
			return true;
		}

		bool SendPacket(
			const FClientSessionId sessionId,
			const NetworkLib::Packet::Serialization::IContentPacket& packet,
			const std::uint8_t randomKey,
			std::string& outErrorMessage)
		{
			std::vector<char> packetBuffer;
			if (!BuildPacketBuffer(packet, randomKey, packetBuffer, outErrorMessage))
			{
				return false;
			}

			return SendPacketBuffer(sessionId, std::move(packetBuffer), outErrorMessage);
		}

		bool SendPacketBuffer(
			const FClientSessionId sessionId,
			std::vector<char>&& packetBuffer,
			std::string& outErrorMessage)
		{
			FClientSession* session = AcquireSession(sessionId);
			if (session == nullptr)
			{
				outErrorMessage = "session not found.";
				return false;
			}

			bool sendQueued = true;
			int sendErrorCode = 0;
			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				if (session->IsClosing())
				{
					outErrorMessage = "session is closing.";
					sendQueued = false;
				}
				else
				{
					session->SendQueue.emplace_back(std::move(packetBuffer));
					if (!StartNextSendLocked(*session, outErrorMessage, sendErrorCode))
					{
						sendQueued = false;
					}
				}
			}

			if (!sendQueued && sendErrorCode != 0)
			{
				CloseSession(session, EClientEventType::SendFailed, sendErrorCode, outErrorMessage);
			}

			session->ReleaseRef();
			return sendQueued;
		}

		std::size_t PollEvents(
			std::vector<FClientEvent>& outEvents,
			const std::size_t maxEventCount)
		{
			std::lock_guard<std::mutex> eventLock(EventMutex);
			const std::size_t availableEventCount = std::min(maxEventCount, Events.size());
			for (std::size_t index = 0; index < availableEventCount; ++index)
			{
				outEvents.push_back(std::move(Events.front()));
				Events.pop_front();
			}

			return availableEventCount;
		}

		bool TryPopEvent(
			FClientEvent& outEvent)
		{
			std::lock_guard<std::mutex> eventLock(EventMutex);
			if (Events.empty())
			{
				return false;
			}

			outEvent = std::move(Events.front());
			Events.pop_front();
			return true;
		}

		std::size_t GetActiveSessionCount() const
		{
			std::lock_guard<std::mutex> sessionLock(SessionMutex);
			return Sessions.size();
		}

		const FClientNetworkConfig& GetConfig() const noexcept
		{
			return Config;
		}

	private:
		bool BuildPacketBuffer(
			const NetworkLib::Packet::Serialization::IContentPacket& packet,
			const std::uint8_t randomKey,
			std::vector<char>& outPacketBuffer,
			std::string& outErrorMessage)
		{
			std::vector<char> serializedPayload = NetworkLib::Packet::Serialization::SerializeContentPacket(packet);
			if (serializedPayload.empty())
			{
				outErrorMessage = "SerializeContentPacket failed.";
				return false;
			}

			PacketCipher.Encode(serializedPayload.data(), static_cast<int>(serializedPayload.size()), randomKey);

			NetworkLib::Packet::Framing::SOutgoingPacket outgoingPacket{};
			outgoingPacket.randomKey = randomKey;
			outgoingPacket.checkSum = PacketCipher.CalculateChecksum(serializedPayload.data(), static_cast<int>(serializedPayload.size()));
			outgoingPacket.payload = serializedPayload.data();
			outgoingPacket.payloadLength = static_cast<std::int32_t>(serializedPayload.size());

			if (!PacketFramer.BuildPacket(outgoingPacket, outPacketBuffer))
			{
				outErrorMessage = "BuildPacket failed.";
				return false;
			}

			return true;
		}

		void WorkerLoop()
		{
			while (true)
			{
				DWORD transferredBytes = 0;
				ULONG_PTR completionKey = 0;
				OVERLAPPED* overlapped = nullptr;
				const BOOL completionSucceeded = GetQueuedCompletionStatus(IocpHandle, &transferredBytes, &completionKey, &overlapped, 100);

				if (overlapped == nullptr)
				{
					if (!Running.load(std::memory_order_acquire) && InFlightIoCount.load(std::memory_order_acquire) == 0)
					{
						break;
					}

					(void)completionKey;
					continue;
				}

				auto* ioContext = reinterpret_cast<FClientSession::FIoContext*>(overlapped);
				FClientSession* session = ioContext->OwnerSession;
				const int errorCode = completionSucceeded ? 0 : static_cast<int>(GetLastError());

				switch (ioContext->IoType)
				{
					case FClientSession::EIoType::Recv:
						if (!completionSucceeded || transferredBytes == 0)
						{
							HandleRecvFailure(session, errorCode, transferredBytes == 0);
						}
						else
						{
							HandleRecvCompletion(session, transferredBytes);
						}
						break;

					case FClientSession::EIoType::Send:
						if (!completionSucceeded || transferredBytes == 0)
						{
							HandleSendFailure(session, errorCode, transferredBytes == 0);
						}
						else
						{
							HandleSendCompletion(session, transferredBytes);
						}
						break;
				}

				InFlightIoCount.fetch_sub(1, std::memory_order_acq_rel);
				session->ReleaseRef();
			}
		}

		void HandleRecvFailure(
			FClientSession* session,
			const int errorCode,
			const bool remoteClosed)
		{
			if (session == nullptr)
			{
				return;
			}

			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				session->RecvPosted = false;
			}

			const std::string message =
				remoteClosed ? std::string("remote peer closed the connection.") : BuildWinsockErrorMessage("WSARecv failed.", errorCode);
			CloseSession(session, remoteClosed ? EClientEventType::Disconnected : EClientEventType::SessionError, errorCode, message);
		}

		void HandleSendFailure(
			FClientSession* session,
			const int errorCode,
			const bool zeroByteCompletion)
		{
			if (session == nullptr)
			{
				return;
			}

			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				session->SendInFlight = false;
			}

			const std::string message = zeroByteCompletion ? std::string("WSASend completed with zero bytes.")
														   : BuildWinsockErrorMessage("WSASend failed.", errorCode);
			CloseSession(session, EClientEventType::SendFailed, errorCode, message);
		}

		void HandleRecvCompletion(
			FClientSession* session,
			const DWORD transferredBytes)
		{
			if (session == nullptr)
			{
				return;
			}

			std::vector<FClientEvent> packetEvents;
			bool shouldClose = false;
			int closeErrorCode = 0;
			std::string closeMessage;
			EClientEventType closeEventType = EClientEventType::SessionError;

			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				session->RecvPosted = false;
				if (session->IsClosing())
				{
					return;
				}

				session->InboundBuffer.insert(session->InboundBuffer.end(),
					session->RecvScratchBuffer.begin(),
					session->RecvScratchBuffer.begin() + static_cast<std::ptrdiff_t>(transferredBytes));

				NetworkLib::Packet::Framing::SFramedPacket framedPacket{};
				while (PacketFramer.TryExtractPacket(session->InboundBuffer, framedPacket))
				{
					if (Config.ValidatePacketChecksum)
					{
						const std::uint8_t calculatedChecksum =
							PacketCipher.CalculateChecksum(framedPacket.payload.data(), static_cast<int>(framedPacket.payload.size()));
						if (calculatedChecksum != framedPacket.checkSum)
						{
							closeMessage = "packet checksum validation failed.";
							closeEventType = EClientEventType::SessionError;
							shouldClose = true;
							break;
						}
					}

					PacketCipher.Decode(framedPacket.payload.data(), static_cast<int>(framedPacket.payload.size()), framedPacket.randomKey);

					NetworkLib::Packet::View::FPacketView transportPacketView{};
					transportPacketView.randomKey = framedPacket.randomKey;
					transportPacketView.checkSum = framedPacket.checkSum;
					transportPacketView.payload = framedPacket.payload.data();
					transportPacketView.payloadLength = static_cast<std::int32_t>(framedPacket.payload.size());

					NetworkLib::Packet::View::FPacketView contentPacketView{};
					if (!NetworkLib::Packet::Serialization::TryParseContentPacketView(transportPacketView, contentPacketView))
					{
						closeMessage = "content packet header parse failed.";
						closeEventType = EClientEventType::SessionError;
						shouldClose = true;
						break;
					}

					FClientEvent packetEvent = BuildEvent(EClientEventType::PacketReceived, session->SessionId, 0, {});
					packetEvent.Packet.Opcode = contentPacketView.opcode;
					packetEvent.Packet.RandomKey = contentPacketView.randomKey;
					packetEvent.Packet.Checksum = contentPacketView.checkSum;
					packetEvent.Packet.Payload.assign(
						contentPacketView.payload, contentPacketView.payload + contentPacketView.payloadLength);
					packetEvents.push_back(std::move(packetEvent));
				}

				if (!shouldClose)
				{
					if (!PostRecvLocked(*session, closeMessage, closeErrorCode))
					{
						closeEventType = EClientEventType::SessionError;
						shouldClose = true;
					}
				}
			}

			for (FClientEvent& packetEvent : packetEvents)
			{
				QueueEvent(std::move(packetEvent));
			}

			if (shouldClose)
			{
				CloseSession(session, closeEventType, closeErrorCode, closeMessage);
			}
		}

		void HandleSendCompletion(
			FClientSession* session,
			const DWORD transferredBytes)
		{
			if (session == nullptr)
			{
				return;
			}

			bool shouldClose = false;
			int closeErrorCode = 0;
			std::string closeMessage;
			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				if (session->IsClosing())
				{
					return;
				}

				session->ActiveSendOffset += static_cast<std::size_t>(transferredBytes);
				if (session->ActiveSendOffset < session->ActiveSendBuffer.size())
				{
					if (!SubmitActiveSendLocked(*session, closeMessage, closeErrorCode))
					{
						session->SendInFlight = false;
						shouldClose = true;
					}
				}
				else
				{
					session->ActiveSendBuffer.clear();
					session->ActiveSendOffset = 0;
					session->SendInFlight = false;
					if (!StartNextSendLocked(*session, closeMessage, closeErrorCode))
					{
						shouldClose = true;
					}
				}
			}

			if (shouldClose)
			{
				CloseSession(session, EClientEventType::SendFailed, closeErrorCode, closeMessage);
			}
		}

		bool PostRecvLocked(
			FClientSession& session,
			std::string& outErrorMessage,
			int& outErrorCode)
		{
			outErrorCode = 0;
			if (session.IsClosing())
			{
				outErrorMessage = "session is closing.";
				return false;
			}

			if (session.RecvPosted)
			{
				return true;
			}

			if (session.SocketHandle == INVALID_SOCKET)
			{
				outErrorMessage = "socket is not connected.";
				return false;
			}

			session.RecvContext.Prepare(FClientSession::EIoType::Recv, &session);

			WSABUF recvBuffer{};
			recvBuffer.buf = session.RecvScratchBuffer.data();
			recvBuffer.len = static_cast<ULONG>(session.RecvScratchBuffer.size());

			DWORD flags = 0;
			DWORD receivedBytes = 0;
			session.AcquireRef();
			InFlightIoCount.fetch_add(1, std::memory_order_acq_rel);
			const int recvResult =
				WSARecv(session.SocketHandle, &recvBuffer, 1, &receivedBytes, &flags, &session.RecvContext.Overlapped, nullptr);
			if (recvResult == SOCKET_ERROR)
			{
				outErrorCode = WSAGetLastError();
				if (outErrorCode != WSA_IO_PENDING)
				{
					InFlightIoCount.fetch_sub(1, std::memory_order_acq_rel);
					session.ReleaseRef();
					outErrorMessage = BuildWinsockErrorMessage("WSARecv failed.", outErrorCode);
					return false;
				}
			}

			session.RecvPosted = true;
			return true;
		}

		bool StartNextSendLocked(
			FClientSession& session,
			std::string& outErrorMessage,
			int& outErrorCode)
		{
			if (session.IsClosing())
			{
				outErrorMessage = "session is closing.";
				return false;
			}

			if (session.SendInFlight)
			{
				return true;
			}

			if (session.ActiveSendBuffer.empty())
			{
				if (session.SendQueue.empty())
				{
					return true;
				}

				session.ActiveSendBuffer = std::move(session.SendQueue.front());
				session.SendQueue.pop_front();
				session.ActiveSendOffset = 0;
			}

			session.SendInFlight = true;
			if (!SubmitActiveSendLocked(session, outErrorMessage, outErrorCode))
			{
				session.SendInFlight = false;
				return false;
			}

			return true;
		}

		bool SubmitActiveSendLocked(
			FClientSession& session,
			std::string& outErrorMessage,
			int& outErrorCode)
		{
			outErrorCode = 0;
			if (session.SocketHandle == INVALID_SOCKET)
			{
				outErrorMessage = "socket is not connected.";
				return false;
			}

			if (session.ActiveSendBuffer.empty() || session.ActiveSendOffset >= session.ActiveSendBuffer.size())
			{
				outErrorMessage = "active send buffer is empty.";
				return false;
			}

			session.SendContext.Prepare(FClientSession::EIoType::Send, &session);

			WSABUF sendBuffer{};
			sendBuffer.buf = session.ActiveSendBuffer.data() + static_cast<std::ptrdiff_t>(session.ActiveSendOffset);
			sendBuffer.len = static_cast<ULONG>(session.ActiveSendBuffer.size() - session.ActiveSendOffset);

			DWORD sentBytes = 0;
			session.AcquireRef();
			InFlightIoCount.fetch_add(1, std::memory_order_acq_rel);
			const int sendResult = WSASend(session.SocketHandle, &sendBuffer, 1, &sentBytes, 0, &session.SendContext.Overlapped, nullptr);
			if (sendResult == SOCKET_ERROR)
			{
				outErrorCode = WSAGetLastError();
				if (outErrorCode != WSA_IO_PENDING)
				{
					InFlightIoCount.fetch_sub(1, std::memory_order_acq_rel);
					session.ReleaseRef();
					outErrorMessage = BuildWinsockErrorMessage("WSASend failed.", outErrorCode);
					return false;
				}
			}

			return true;
		}

		FClientSession* AcquireSession(
			const FClientSessionId sessionId)
		{
			std::lock_guard<std::mutex> sessionLock(SessionMutex);
			const auto sessionIt = Sessions.find(sessionId);
			if (sessionIt == Sessions.end())
			{
				return nullptr;
			}

			FClientSession* session = sessionIt->second;
			session->AcquireRef();
			return session;
		}

		void RemoveSession(
			FClientSession* session)
		{
			if (session == nullptr)
			{
				return;
			}

			std::lock_guard<std::mutex> sessionLock(SessionMutex);
			const auto sessionIt = Sessions.find(session->SessionId);
			if (sessionIt != Sessions.end() && sessionIt->second == session)
			{
				Sessions.erase(sessionIt);
			}
		}

		void CloseSession(
			FClientSession* session,
			const EClientEventType primaryEventType,
			const int errorCode,
			const std::string& message)
		{
			if (session == nullptr)
			{
				return;
			}

			if (!session->TryMarkClosing())
			{
				return;
			}

			RemoveSession(session);
			{
				std::lock_guard<std::mutex> sessionLock(session->Mutex);
				session->RecvPosted = false;
				session->SendInFlight = false;
				session->SendQueue.clear();
				session->ActiveSendBuffer.clear();
				session->ActiveSendOffset = 0;
			}

			if (primaryEventType != EClientEventType::Disconnected)
			{
				QueueEvent(BuildEvent(primaryEventType, session->SessionId, errorCode, message));
			}

			QueueEvent(BuildEvent(EClientEventType::Disconnected, session->SessionId, errorCode, message));
			session->CloseSocket();
			session->ReleaseRef();
		}

		FClientEvent BuildEvent(
			const EClientEventType type,
			const FClientSessionId sessionId,
			const int errorCode,
			const std::string& message) const
		{
			FClientEvent event{};
			event.Type = type;
			event.SessionId = sessionId;
			event.ErrorCode = errorCode;
			event.Message = message;
			event.TimestampSteady = std::chrono::steady_clock::now();
			event.TimestampSystem = std::chrono::system_clock::now();
			return event;
		}

		void QueueEvent(
			FClientEvent&& event)
		{
			std::lock_guard<std::mutex> eventLock(EventMutex);
			Events.push_back(std::move(event));
		}

		void QueueEvent(
			const FClientEvent& event)
		{
			std::lock_guard<std::mutex> eventLock(EventMutex);
			Events.push_back(event);
		}

		FClientNetworkConfig Config;
		NetworkLib::Crypto::FDefaultPacketCipher PacketCipher;
		NetworkLib::Packet::Framing::FDefaultPacketFramer PacketFramer;
		HANDLE IocpHandle = nullptr;
		std::vector<std::thread> WorkerThreads;
		std::atomic<bool> Running = false;
		std::atomic<std::uint64_t> InFlightIoCount = 0;
		std::atomic<FClientSessionId> NextSessionId = 1;
		mutable std::mutex LifecycleMutex;
		mutable std::mutex SessionMutex;
		std::unordered_map<FClientSessionId, FClientSession*> Sessions;
		mutable std::mutex EventMutex;
		std::deque<FClientEvent> Events;
	};

	FClientNetwork::FClientNetwork(
		const FClientNetworkConfig& config)
		: m_impl(std::make_unique<FImpl>(config))
	{
	}

	FClientNetwork::~FClientNetwork() = default;

	bool FClientNetwork::Start(
		std::string& outErrorMessage)
	{
		return m_impl->Start(outErrorMessage);
	}

	void FClientNetwork::Stop()
	{
		m_impl->Stop();
	}

	bool FClientNetwork::IsRunning() const noexcept
	{
		return m_impl->IsRunning();
	}

	const FClientNetworkConfig& FClientNetwork::GetConfig() const noexcept
	{
		return m_impl->GetConfig();
	}

	bool FClientNetwork::ConnectSession(
		FClientSessionId& outSessionId,
		std::string& outErrorMessage)
	{
		return m_impl->ConnectSession(outSessionId, outErrorMessage);
	}

	bool FClientNetwork::DisconnectSession(
		const FClientSessionId sessionId,
		const std::string& reasonMessage)
	{
		return m_impl->DisconnectSession(sessionId, reasonMessage);
	}

	bool FClientNetwork::SendPacket(
		const FClientSessionId sessionId,
		const NetworkLib::Packet::Serialization::IContentPacket& packet,
		const std::uint8_t randomKey,
		std::string& outErrorMessage)
	{
		return m_impl->SendPacket(sessionId, packet, randomKey, outErrorMessage);
	}

	bool FClientNetwork::SendPacketBuffer(
		const FClientSessionId sessionId,
		std::vector<char>&& packetBuffer,
		std::string& outErrorMessage)
	{
		return m_impl->SendPacketBuffer(sessionId, std::move(packetBuffer), outErrorMessage);
	}

	std::size_t FClientNetwork::PollEvents(
		std::vector<FClientEvent>& outEvents,
		const std::size_t maxEventCount)
	{
		return m_impl->PollEvents(outEvents, maxEventCount);
	}

	bool FClientNetwork::TryPopEvent(
		FClientEvent& outEvent)
	{
		return m_impl->TryPopEvent(outEvent);
	}

	std::size_t FClientNetwork::GetActiveSessionCount() const
	{
		return m_impl->GetActiveSessionCount();
	}
}
