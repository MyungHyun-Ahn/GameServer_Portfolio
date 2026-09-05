#include "RpcLibPch.h"

#include "Client/FOutboundRpcClient.h"

#include "Transport/FClientRpcTransport.h"

namespace RpcLib::Client
{
	namespace
	{
		constexpr std::chrono::milliseconds kEventPollInterval{1};

		ClientNetworkLib::FClientNetworkConfig BuildNetworkConfig(
			const FOutboundRpcClientConfig& config)
		{
			ClientNetworkLib::FClientNetworkConfig networkConfig;
			networkConfig.ServerIp = config.host;
			networkConfig.ServerPort = config.port;
			networkConfig.WorkerThreadCount = config.networkWorkerThreadCount;
			networkConfig.RecvScratchBufferSize = config.recvScratchBufferSize;
			networkConfig.PacketCipherConfig.packetKey = config.packetKey;
			return networkConfig;
		}

		bool ValidateConfig(
			const FOutboundRpcClientConfig& config,
			std::string& outError)
		{
			if (config.host.empty())
			{
				outError = "outbound RPC host is empty.";
				return false;
			}
			if (config.port == 0)
			{
				outError = "outbound RPC port must not be zero.";
				return false;
			}
			if (config.localServerType == Protocol::ERpcServerType::Unknown || config.localServerInstanceId == 0)
			{
				outError = "outbound RPC local server identity is invalid.";
				return false;
			}
			if (config.expectedRemoteServerType == Protocol::ERpcServerType::Unknown || config.expectedRemoteServerInstanceId == 0)
			{
				outError = "outbound RPC expected remote server identity is invalid.";
				return false;
			}
			if (config.handshakeTimeout <= std::chrono::milliseconds::zero())
			{
				outError = "outbound RPC handshake timeout must be positive.";
				return false;
			}
			if (config.reconnectInterval <= std::chrono::milliseconds::zero())
			{
				outError = "outbound RPC reconnect interval must be positive.";
				return false;
			}
			if (config.networkWorkerThreadCount == 0 || config.recvScratchBufferSize == 0)
			{
				outError = "outbound RPC ClientNetworkLib configuration is invalid.";
				return false;
			}

			outError.clear();
			return true;
		}

		bool IsDisconnectEvent(
			const ClientNetworkLib::EClientEventType eventType) noexcept
		{
			return eventType == ClientNetworkLib::EClientEventType::Disconnected ||
				   eventType == ClientNetworkLib::EClientEventType::SendFailed ||
				   eventType == ClientNetworkLib::EClientEventType::SessionError;
		}
	}

	struct FOutboundRpcClient::FImpl final
	{
		explicit FImpl(
			FOutboundRpcClientConfig clientConfig)
			: config(std::move(clientConfig))
			, network(BuildNetworkConfig(config))
			, transport(network, config.randomKey)
		{
		}

		bool Start(
			std::string& outError)
		{
			std::unique_lock lifecycleLock(lifecycleMutex);
			if (stopping)
			{
				outError = "outbound RPC client is stopping.";
				return false;
			}
			if (running.load(std::memory_order_acquire))
			{
				outError.clear();
				return true;
			}
			if (workerThread.joinable())
			{
				workerThread.join();
			}

			if (!ValidateConfig(config, outError))
			{
				SetLastError(outError);
				return false;
			}

			ClientNetworkLib::FClientEvent staleEvent;
			while (network.TryPopEvent(staleEvent))
			{
			}

			if (!network.Start(outError))
			{
				SetLastError(outError);
				return false;
			}

			stopRequested.store(false, std::memory_order_release);
			running.store(true, std::memory_order_release);
			try
			{
				workerThread = std::thread(
					[this]()
					{
						RunBackgroundLoop();
					});
				workerThreadId = workerThread.get_id();
			}
			catch (const std::exception& exception)
			{
				running.store(false, std::memory_order_release);
				stopRequested.store(true, std::memory_order_release);
				network.Stop();
				outError = exception.what();
				SetLastError(outError);
				return false;
			}

			outError.clear();
			return true;
		}

		void Stop()
		{
			std::thread threadToJoin;
			{
				std::unique_lock lifecycleLock(lifecycleMutex);
				stopRequested.store(true, std::memory_order_release);
				wakeCondition.notify_all();

				if (std::this_thread::get_id() == workerThreadId)
				{
					return;
				}

				lifecycleCondition.wait(lifecycleLock,
					[this]()
					{
						return !stopping;
					});
				if (!workerThread.joinable())
				{
					running.store(false, std::memory_order_release);
					return;
				}

				stopping = true;
				threadToJoin = std::move(workerThread);
			}

			threadToJoin.join();

			{
				std::lock_guard lifecycleLock(lifecycleMutex);
				workerThreadId = {};
				stopping = false;
				running.store(false, std::memory_order_release);
			}
			lifecycleCondition.notify_all();
		}

		void RunBackgroundLoop() noexcept
		{
			try
			{
				RunEventLoop();
			}
			catch (const std::exception& exception)
			{
				SetLastError(exception.what());
			}
			catch (...)
			{
				SetLastError("outbound RPC background loop failed with an unknown exception.");
			}

			CloseCurrentSession("outbound RPC client stopped.", false);
			network.Stop();
			running.store(false, std::memory_order_release);
		}

		void RunEventLoop()
		{
			auto nextReconnectAt = std::chrono::steady_clock::now();
			auto handshakeDeadline = std::chrono::steady_clock::time_point::max();

			while (!stopRequested.load(std::memory_order_acquire))
			{
				const auto now = std::chrono::steady_clock::now();
				if (connectedSessionId.load(std::memory_order_acquire) == 0 && now >= nextReconnectAt)
				{
					if (TryConnect(handshakeDeadline))
					{
						nextReconnectAt = std::chrono::steady_clock::time_point::max();
					}
					else
					{
						nextReconnectAt = std::chrono::steady_clock::now() + config.reconnectInterval;
					}
				}

				ClientNetworkLib::FClientEvent event;
				while (network.TryPopEvent(event))
				{
					if (HandleEvent(event, handshakeDeadline))
					{
						nextReconnectAt = std::chrono::steady_clock::now() + config.reconnectInterval;
					}
				}

				const std::uint64_t sessionId = connectedSessionId.load(std::memory_order_acquire);
				if (sessionId != 0 && readySessionId.load(std::memory_order_acquire) == 0 &&
					std::chrono::steady_clock::now() >= handshakeDeadline)
				{
					CloseCurrentSession("outbound RPC Hello handshake timed out.", true);
					nextReconnectAt = std::chrono::steady_clock::now() + config.reconnectInterval;
					handshakeDeadline = std::chrono::steady_clock::time_point::max();
				}

				std::unique_lock wakeLock(wakeMutex);
				wakeCondition.wait_for(wakeLock,
					kEventPollInterval,
					[this]()
					{
						return stopRequested.load(std::memory_order_acquire);
					});
			}
		}

		bool TryConnect(
			std::chrono::steady_clock::time_point& outHandshakeDeadline)
		{
			std::string error;
			ClientNetworkLib::FClientSessionId sessionId = 0;
			if (!network.ConnectSession(sessionId, error))
			{
				SetLastError(error);
				return false;
			}

			if (stopRequested.load(std::memory_order_acquire))
			{
				network.DisconnectSession(sessionId, "outbound RPC client is stopping.");
				return false;
			}

			if (!sessionRegistry.Add(sessionId))
			{
				SetLastError("outbound RPC session registration failed.");
				network.DisconnectSession(sessionId, "RPC session registration failed.");
				return false;
			}

			const std::shared_ptr<Session::FRpcSession> rpcSession = sessionRegistry.Find(sessionId);
			if (rpcSession == nullptr || !rpcSession->BeginHandshake())
			{
				SetLastError("outbound RPC handshake state transition failed.");
				sessionRegistry.Remove(sessionId);
				network.DisconnectSession(sessionId, "RPC handshake state transition failed.");
				return false;
			}

			connectedSessionId.store(sessionId, std::memory_order_release);
			outHandshakeDeadline = std::chrono::steady_clock::now() + config.handshakeTimeout;

			Protocol::FRpcHelloRequest helloRequest;
			helloRequest.protocolVersion = Protocol::kRpcProtocolVersion;
			helloRequest.serverType = config.localServerType;
			helloRequest.serverInstanceId = config.localServerInstanceId;
			if (!transport.SendHelloRequest(sessionId, helloRequest))
			{
				CloseCurrentSession("outbound RPC Hello request send failed.", true);
				outHandshakeDeadline = std::chrono::steady_clock::time_point::max();
				return false;
			}

			return true;
		}

		bool HandleEvent(
			const ClientNetworkLib::FClientEvent& event,
			std::chrono::steady_clock::time_point& inOutHandshakeDeadline)
		{
			const std::uint64_t sessionId = connectedSessionId.load(std::memory_order_acquire);
			if (event.Type == ClientNetworkLib::EClientEventType::ConnectFailed)
			{
				SetLastError(event.Message);
				return sessionId == 0;
			}
			if (event.SessionId == 0 || event.SessionId != sessionId)
			{
				return false;
			}
			if (IsDisconnectEvent(event.Type))
			{
				CloseCurrentSession(event.Message.empty() ? "outbound RPC connection closed." : event.Message, true);
				inOutHandshakeDeadline = std::chrono::steady_clock::time_point::max();
				return true;
			}
			if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived)
			{
				return false;
			}

			const auto opcode = static_cast<Protocol::ERpcWireOpcode>(event.Packet.Opcode);
			if (readySessionId.load(std::memory_order_acquire) == 0)
			{
				if (opcode != Protocol::ERpcWireOpcode::HelloResponse || !CompleteHandshake(sessionId, event.Packet.Payload))
				{
					CloseCurrentSession("outbound RPC received an invalid Hello response.", true);
					inOutHandshakeDeadline = std::chrono::steady_clock::time_point::max();
					return true;
				}

				inOutHandshakeDeadline = std::chrono::steady_clock::time_point::max();
				return false;
			}

			switch (opcode)
			{
				case Protocol::ERpcWireOpcode::Response:
				{
					Protocol::FRpcResponse response;
					if (!Protocol::DeserializeRpcResponse(event.Packet.Payload, response) ||
						response.protocolVersion != Protocol::kRpcProtocolVersion)
					{
						CloseCurrentSession("outbound RPC received an invalid response payload.", true);
						return true;
					}

					InvokeResponseCallback(sessionId, response);
					return false;
				}
				case Protocol::ERpcWireOpcode::Request:
				{
					Protocol::FRpcRequest request;
					if (!Protocol::DeserializeRpcRequest(event.Packet.Payload, request) ||
						request.protocolVersion != Protocol::kRpcProtocolVersion)
					{
						CloseCurrentSession("outbound RPC received an invalid request payload.", true);
						return true;
					}

					InvokeRequestCallback(sessionId, request);
					return false;
				}
				case Protocol::ERpcWireOpcode::Notification:
				{
					Protocol::FRpcNotification notification;
					if (!Protocol::DeserializeRpcNotification(event.Packet.Payload, notification) ||
						notification.protocolVersion != Protocol::kRpcProtocolVersion)
					{
						CloseCurrentSession("outbound RPC received an invalid notification payload.", true);
						return true;
					}

					InvokeNotificationCallback(sessionId, notification);
					return false;
				}
				default:
					CloseCurrentSession("outbound RPC received an unexpected wire opcode.", true);
					return true;
			}
		}

		bool CompleteHandshake(
			const std::uint64_t sessionId,
			const std::span<const char> payload)
		{
			Protocol::FRpcHelloResponse response;
			if (!Protocol::DeserializeRpcHelloResponse(payload, response) || response.protocolVersion != Protocol::kRpcProtocolVersion ||
				response.result != Protocol::ERpcHelloResult::Success || response.serverType != config.expectedRemoteServerType ||
				response.serverInstanceId != config.expectedRemoteServerInstanceId ||
				!sessionRegistry.MarkReady(sessionId, response.serverType, response.serverInstanceId, response.protocolVersion))
			{
				return false;
			}

			readySessionId.store(sessionId, std::memory_order_release);
			SetLastError({});
			InvokeSessionCallback(readyCallback, sessionId);
			return true;
		}

		void CloseCurrentSession(
			const std::string& reason,
			const bool invokeCallback)
		{
			const std::uint64_t sessionId = connectedSessionId.exchange(0, std::memory_order_acq_rel);
			if (sessionId == 0)
			{
				return;
			}

			readySessionId.store(0, std::memory_order_release);
			sessionRegistry.Remove(sessionId);
			network.DisconnectSession(sessionId, reason);
			SetLastError(reason);
			if (invokeCallback && !stopRequested.load(std::memory_order_acquire))
			{
				InvokeSessionCallback(disconnectCallback, sessionId);
			}
		}

		void InvokeResponseCallback(
			const std::uint64_t sessionId,
			const Protocol::FRpcResponse& response) noexcept
		{
			FResponseCallback callback;
			{
				std::lock_guard callbackLock(callbackMutex);
				callback = responseCallback;
			}
			if (!callback || stopRequested.load(std::memory_order_acquire))
			{
				return;
			}

			try
			{
				callback(sessionId, response);
			}
			catch (const std::exception& exception)
			{
				SetLastError(exception.what());
			}
			catch (...)
			{
				SetLastError("outbound RPC response callback failed with an unknown exception.");
			}
		}

		void InvokeRequestCallback(
			const std::uint64_t sessionId,
			const Protocol::FRpcRequest& request) noexcept
		{
			FRequestCallback callback;
			{
				std::lock_guard callbackLock(callbackMutex);
				callback = requestCallback;
			}
			if (!callback || stopRequested.load(std::memory_order_acquire))
			{
				return;
			}

			try
			{
				callback(sessionId, request);
			}
			catch (const std::exception& exception)
			{
				SetLastError(exception.what());
			}
			catch (...)
			{
				SetLastError("outbound RPC request callback failed with an unknown exception.");
			}
		}

		void InvokeNotificationCallback(
			const std::uint64_t sessionId,
			const Protocol::FRpcNotification& notification) noexcept
		{
			FNotificationCallback callback;
			{
				std::lock_guard callbackLock(callbackMutex);
				callback = notificationCallback;
			}
			if (!callback || stopRequested.load(std::memory_order_acquire))
			{
				return;
			}

			try
			{
				callback(sessionId, notification);
			}
			catch (const std::exception& exception)
			{
				SetLastError(exception.what());
			}
			catch (...)
			{
				SetLastError("outbound RPC notification callback failed with an unknown exception.");
			}
		}

		void InvokeSessionCallback(
			const FSessionCallback& storedCallback,
			const std::uint64_t sessionId) noexcept
		{
			FSessionCallback callback;
			{
				std::lock_guard callbackLock(callbackMutex);
				callback = storedCallback;
			}
			if (!callback || stopRequested.load(std::memory_order_acquire))
			{
				return;
			}

			try
			{
				callback(sessionId);
			}
			catch (const std::exception& exception)
			{
				SetLastError(exception.what());
			}
			catch (...)
			{
				SetLastError("outbound RPC session callback failed with an unknown exception.");
			}
		}

		void SetLastError(
			std::string error)
		{
			std::lock_guard errorLock(errorMutex);
			lastError = std::move(error);
		}

		FOutboundRpcClientConfig config;
		ClientNetworkLib::FClientNetwork network;
		Transport::FClientRpcTransport transport;
		Session::FRpcSessionRegistry sessionRegistry;
		Call::FRpcRequestIdGenerator requestIdGenerator;

		std::atomic<bool> running = false;
		std::atomic<bool> stopRequested = false;
		std::atomic<std::uint64_t> connectedSessionId = 0;
		std::atomic<std::uint64_t> readySessionId = 0;
		std::mutex lifecycleMutex;
		std::condition_variable lifecycleCondition;
		std::thread workerThread;
		std::thread::id workerThreadId{};
		bool stopping = false;
		std::mutex wakeMutex;
		std::condition_variable wakeCondition;

		std::mutex callbackMutex;
		FResponseCallback responseCallback;
		FRequestCallback requestCallback;
		FNotificationCallback notificationCallback;
		FSessionCallback disconnectCallback;
		FSessionCallback readyCallback;
		mutable std::mutex errorMutex;
		std::string lastError;
	};

	FOutboundRpcClient::FOutboundRpcClient(
		FOutboundRpcClientConfig config)
		: m_impl(std::make_unique<FImpl>(std::move(config)))
	{
	}

	FOutboundRpcClient::~FOutboundRpcClient()
	{
		Stop();
	}

	bool FOutboundRpcClient::Start(
		std::string& outError)
	{
		return m_impl->Start(outError);
	}

	void FOutboundRpcClient::Stop()
	{
		m_impl->Stop();
	}

	bool FOutboundRpcClient::IsRunning() const noexcept
	{
		return m_impl->running.load(std::memory_order_acquire);
	}

	bool FOutboundRpcClient::IsReady() const noexcept
	{
		return GetReadySessionId() != 0;
	}

	std::uint64_t FOutboundRpcClient::GetReadySessionId() const noexcept
	{
		return m_impl->readySessionId.load(std::memory_order_acquire);
	}

	const FOutboundRpcClientConfig& FOutboundRpcClient::GetConfig() const noexcept
	{
		return m_impl->config;
	}

	std::string FOutboundRpcClient::GetLastError() const
	{
		std::lock_guard errorLock(m_impl->errorMutex);
		return m_impl->lastError;
	}

	Session::FRpcSessionRegistry& FOutboundRpcClient::GetSessionRegistry() noexcept
	{
		return m_impl->sessionRegistry;
	}

	Call::FRpcRequestIdGenerator& FOutboundRpcClient::GetRequestIdGenerator() noexcept
	{
		return m_impl->requestIdGenerator;
	}

	Transport::IRpcTransport& FOutboundRpcClient::GetTransport() noexcept
	{
		return m_impl->transport;
	}

	void FOutboundRpcClient::SetResponseCallback(
		FResponseCallback callback)
	{
		std::lock_guard callbackLock(m_impl->callbackMutex);
		m_impl->responseCallback = std::move(callback);
	}

	void FOutboundRpcClient::SetRequestCallback(
		FRequestCallback callback)
	{
		std::lock_guard callbackLock(m_impl->callbackMutex);
		m_impl->requestCallback = std::move(callback);
	}

	void FOutboundRpcClient::SetNotificationCallback(
		FNotificationCallback callback)
	{
		std::lock_guard callbackLock(m_impl->callbackMutex);
		m_impl->notificationCallback = std::move(callback);
	}

	void FOutboundRpcClient::SetDisconnectCallback(
		FSessionCallback callback)
	{
		std::lock_guard callbackLock(m_impl->callbackMutex);
		m_impl->disconnectCallback = std::move(callback);
	}

	void FOutboundRpcClient::SetReadyCallback(
		FSessionCallback callback)
	{
		std::lock_guard callbackLock(m_impl->callbackMutex);
		m_impl->readyCallback = std::move(callback);
	}
}
