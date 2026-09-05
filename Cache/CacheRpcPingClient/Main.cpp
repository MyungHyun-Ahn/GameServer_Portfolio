#include "CacheRpcPingClientPch.h"

#include "CacheRpcPingClient/UserPresenceSmoke.h"

namespace
{
	constexpr std::uint8_t kPacketKey = 0x37;
	constexpr std::uint8_t kRandomKey = 0x51;

	struct SOptions final
	{
		std::string serverIp = "127.0.0.1";
		std::uint16_t port = 19103;
		std::uint32_t pingCount = 16;
		std::uint32_t expectedShardCount = 4;
		std::uint64_t loadUserId = 0;
		std::uint64_t loadRoutingKey = 0;
		std::uint32_t expectedCurrencyCount = 0;
		std::uint32_t expectedInventoryCount = 0;
		bool expectInvalidLoad = false;
		RpcLib::Protocol::FRpcServerInstanceId clientInstanceId = 1;
		RpcLib::Protocol::FRpcServerInstanceId cacheInstanceId = 1;
		std::uint32_t timeoutMilliseconds = 5000;
	};

	bool TryParseUnsigned(
		const char* text,
		std::uint64_t& outValue)
	{
		if (text == nullptr || *text == '\0')
		{
			return false;
		}

		char* end = nullptr;
		const unsigned long long value = std::strtoull(text, &end, 10);
		if (end == text || *end != '\0')
		{
			return false;
		}

		outValue = static_cast<std::uint64_t>(value);
		return true;
	}

	bool TryParseOptions(
		const int argc,
		char* argv[],
		SOptions& outOptions)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument = argv[index];
			if (argument == "--host" && index + 1 < argc)
			{
				outOptions.serverIp = argv[++index];
			}
			else if (argument == "--port" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > 65535)
				{
					return false;
				}
				outOptions.port = static_cast<std::uint16_t>(value);
			}
			else if (argument == "--ping-count" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > 1000)
				{
					return false;
				}
				outOptions.pingCount = static_cast<std::uint32_t>(value);
			}
			else if (argument == "--expected-shards" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > 64)
				{
					return false;
				}
				outOptions.expectedShardCount = static_cast<std::uint32_t>(value);
			}
			else if (argument == "--load-user-id" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0)
				{
					return false;
				}
				outOptions.loadUserId = value;
			}
			else if (argument == "--load-routing-key" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0)
				{
					return false;
				}
				outOptions.loadRoutingKey = value;
			}
			else if (argument == "--expected-currency-count" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.expectedCurrencyCount = static_cast<std::uint32_t>(value);
			}
			else if (argument == "--expected-inventory-count" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.expectedInventoryCount = static_cast<std::uint32_t>(value);
			}
			else if (argument == "--expect-invalid-load")
			{
				outOptions.expectInvalidLoad = true;
			}
			else if (argument == "--client-instance-id" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.clientInstanceId = static_cast<RpcLib::Protocol::FRpcServerInstanceId>(value);
			}
			else if (argument == "--cache-instance-id" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.cacheInstanceId = static_cast<RpcLib::Protocol::FRpcServerInstanceId>(value);
			}
			else if (argument == "--timeout-ms" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > 60000)
				{
					return false;
				}
				outOptions.timeoutMilliseconds = static_cast<std::uint32_t>(value);
			}
			else
			{
				return false;
			}
		}

		return !outOptions.serverIp.empty();
	}

	std::uint64_t GetUnixTimeMilliseconds() noexcept
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	}

	class FClientRpcTransport final : public RpcLib::Transport::IRpcTransport
	{
	public:
		explicit FClientRpcTransport(
			ClientNetworkLib::FClientNetwork& network) noexcept
			: m_network(network)
		{
		}

		void SetSessionId(
			const ClientNetworkLib::FClientSessionId sessionId) noexcept
		{
			m_sessionId = sessionId;
		}

		bool SendHelloRequest(
			const RpcLib::Protocol::FRpcHelloRequest& request)
		{
			std::vector<char> payload;
			return RpcLib::Protocol::SerializeRpcHelloRequest(request, payload) &&
				   SendPayload(RpcLib::Protocol::ERpcWireOpcode::HelloRequest, std::move(payload));
		}

		bool SendRequest(
			const std::uint64_t networkSessionId,
			const RpcLib::Protocol::FRpcRequest& request) override
		{
			if (networkSessionId != m_sessionId)
			{
				return false;
			}

			std::vector<char> payload;
			return RpcLib::Protocol::SerializeRpcRequest(request, payload) &&
				   SendPayload(RpcLib::Protocol::ERpcWireOpcode::Request, std::move(payload));
		}

		bool SendResponse(
			const std::uint64_t networkSessionId,
			const RpcLib::Protocol::FRpcResponse& response) override
		{
			if (networkSessionId != m_sessionId)
			{
				return false;
			}

			std::vector<char> payload;
			return RpcLib::Protocol::SerializeRpcResponse(response, payload) &&
				   SendPayload(RpcLib::Protocol::ERpcWireOpcode::Response, std::move(payload));
		}

		bool SendNotification(
			const std::uint64_t networkSessionId,
			const RpcLib::Protocol::FRpcNotification& notification) override
		{
			if (networkSessionId != m_sessionId)
			{
				return false;
			}

			std::vector<char> payload;
			return RpcLib::Protocol::SerializeRpcNotification(notification, payload) &&
				   SendPayload(RpcLib::Protocol::ERpcWireOpcode::Notification, std::move(payload));
		}

		const std::string& GetLastError() const noexcept
		{
			return m_lastError;
		}

	private:
		bool SendPayload(
			const RpcLib::Protocol::ERpcWireOpcode opcode,
			std::vector<char>&& payload)
		{
			if (m_sessionId == 0)
			{
				m_lastError = "RPC transport has no connected session.";
				return false;
			}

			RpcLib::Protocol::FRpcWirePacket packet(opcode, std::move(payload));
			m_lastError.clear();
			return m_network.SendPacket(m_sessionId, packet, kRandomKey, m_lastError);
		}

	private:
		ClientNetworkLib::FClientNetwork& m_network;
		ClientNetworkLib::FClientSessionId m_sessionId = 0;
		std::string m_lastError;
	};

	struct SShardObservation final
	{
		std::uint32_t shardIndex = 0;
		std::uint64_t contentInstanceId = 0;
	};

	struct SLoadObservation final
	{
		std::uint32_t shardIndex = 0;
		std::uint64_t contentInstanceId = 0;
		std::uint32_t currencyCount = 0;
		std::uint32_t inventoryCount = 0;
		std::uint64_t loadedAtUnixMs = 0;
	};
}

int main(
	const int argc,
	char* argv[])
{
	if (argc > 1 && std::string_view(argv[1]) == "--user-presence-smoke")
	{
		return CacheRpcPingClient::RunUserPresenceSmoke(argc, argv);
	}

	SOptions options;
	if (!TryParseOptions(argc, argv, options))
	{
		std::cerr << "Usage: CacheRpcPingClient [--host IP] [--port N] [--ping-count N]"
				  << " [--expected-shards N] [--load-user-id N] [--load-routing-key N] [--expect-invalid-load]"
				  << " [--expected-currency-count N]"
				  << " [--expected-inventory-count N] [--client-instance-id N] [--cache-instance-id N] [--timeout-ms N]\n";
		return 1;
	}

	ClientNetworkLib::FClientNetworkConfig networkConfig{};
	networkConfig.ServerIp = options.serverIp;
	networkConfig.ServerPort = options.port;
	networkConfig.WorkerThreadCount = 2;
	networkConfig.RecvScratchBufferSize = 64 * 1024;
	networkConfig.PacketCipherConfig.packetKey = kPacketKey;

	ClientNetworkLib::FClientNetwork network(networkConfig);
	std::string error;
	if (!network.Start(error))
	{
		std::cerr << "[FAIL] client network start: " << error << '\n';
		return 1;
	}

	ClientNetworkLib::FClientSessionId sessionId = 0;
	if (!network.ConnectSession(sessionId, error))
	{
		std::cerr << "[FAIL] connect: " << error << '\n';
		network.Stop();
		return 1;
	}

	FClientRpcTransport transport(network);
	transport.SetSessionId(sessionId);
	RpcLib::Session::FRpcSessionRegistry sessionRegistry;
	RpcLib::Call::FRpcRequestIdGenerator requestIdGenerator;
	RpcLib::Dispatch::FRpcMethodDispatcher dispatcher;
	RpcLib::FRpcCommon rpcCommon(sessionRegistry, dispatcher, requestIdGenerator, transport, 1, options.pingCount + 8);

	if (!sessionRegistry.Add(sessionId))
	{
		std::cerr << "[FAIL] RPC session registration\n";
		network.Stop();
		return 1;
	}

	const std::shared_ptr<RpcLib::Session::FRpcSession> rpcSession = sessionRegistry.Find(sessionId);
	if (rpcSession == nullptr || !rpcSession->BeginHandshake())
	{
		std::cerr << "[FAIL] RPC handshake state transition\n";
		network.Stop();
		return 1;
	}

	RpcLib::Protocol::FRpcHelloRequest helloRequest;
	helloRequest.serverType = RpcLib::Protocol::ERpcServerType::Auction;
	helloRequest.serverInstanceId = options.clientInstanceId;
	if (!transport.SendHelloRequest(helloRequest))
	{
		std::cerr << "[FAIL] Hello request send: " << transport.GetLastError() << '\n';
		network.Stop();
		return 1;
	}

	bool handshakeCompleted = false;
	bool disconnected = false;
	const auto handshakeDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeoutMilliseconds);
	while (!handshakeCompleted && !disconnected && std::chrono::steady_clock::now() < handshakeDeadline)
	{
		ClientNetworkLib::FClientEvent event;
		while (network.TryPopEvent(event))
		{
			if (event.Type == ClientNetworkLib::EClientEventType::Disconnected ||
				event.Type == ClientNetworkLib::EClientEventType::SessionError ||
				event.Type == ClientNetworkLib::EClientEventType::SendFailed)
			{
				disconnected = true;
				error = event.Message;
				break;
			}

			if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived ||
				event.Packet.Opcode != static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::HelloResponse))
			{
				continue;
			}

			RpcLib::Protocol::FRpcHelloResponse helloResponse;
			if (!RpcLib::Protocol::DeserializeRpcHelloResponse(event.Packet.Payload, helloResponse) ||
				helloResponse.result != RpcLib::Protocol::ERpcHelloResult::Success ||
				helloResponse.serverType != RpcLib::Protocol::ERpcServerType::Cache ||
				helloResponse.serverInstanceId != options.cacheInstanceId ||
				!sessionRegistry.MarkReady(
					sessionId, helloResponse.serverType, helloResponse.serverInstanceId, helloResponse.protocolVersion))
			{
				error = "invalid Hello response.";
				disconnected = true;
				break;
			}

			handshakeCompleted = true;
		}

		if (!handshakeCompleted)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	if (!handshakeCompleted)
	{
		std::cerr << "[FAIL] RPC handshake: " << (error.empty() ? "timeout" : error) << '\n';
		network.Stop();
		return 1;
	}

	constexpr std::uint64_t kNotificationSequence = 900001;
	constexpr std::uint64_t kNotificationUserId = 1;
	const std::uint64_t notificationClientTimeUnixMs = GetUnixTimeMilliseconds();
	RpcLib::Protocol::FRpcTarget notificationTarget;
	notificationTarget.serverType = RpcLib::Protocol::ERpcServerType::Cache;
	notificationTarget.serverInstanceId = options.cacheInstanceId;
	notificationTarget.routingKey = kNotificationUserId;
	const auto notificationResult = rpcCommon.Notify<Cache::Protocol::FCachePingNoti>(
		notificationTarget, kNotificationSequence, kNotificationUserId, notificationClientTimeUnixMs);
	if (!notificationResult.accepted || rpcCommon.GetPendingCallCount() != 0)
	{
		std::cerr << "[FAIL] Cache RPC Notification send error=" << static_cast<int>(notificationResult.error)
				  << " pending=" << rpcCommon.GetPendingCallCount() << '\n';
		network.Stop();
		return 1;
	}

	std::uint32_t successCount = 0;
	std::uint32_t failureCount = 0;
	bool validationFailed = false;
	std::unordered_map<std::uint64_t, SShardObservation> observations;
	const auto requestTimeout = std::chrono::milliseconds(options.timeoutMilliseconds);
	for (std::uint32_t index = 0; index < options.pingCount; ++index)
	{
		const std::uint64_t sequence = static_cast<std::uint64_t>(index) + 1;
		const std::uint64_t userId = static_cast<std::uint64_t>(index % (options.expectedShardCount * 2)) + 1;
		const std::uint64_t clientTimeUnixMs = GetUnixTimeMilliseconds();

		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = options.cacheInstanceId;
		target.routingKey = userId;
		const auto callResult = rpcCommon.Call<Cache::Protocol::FCachePingRpc>(
			target,
			requestTimeout,
			[&, sequence, userId, clientTimeUnixMs](const std::uint64_t responseSequence,
				const std::uint64_t responseUserId,
				const std::uint64_t responseClientTimeUnixMs,
				const std::uint64_t serverTimeUnixMs,
				const std::uint32_t shardIndex,
				const std::uint32_t shardCount,
				const std::uint64_t contentInstanceId,
				const std::uint32_t workerThreadId)
			{
				const std::uint32_t expectedShardIndex = static_cast<std::uint32_t>(userId % options.expectedShardCount);
				const bool valid = responseSequence == sequence && responseUserId == userId &&
								   responseClientTimeUnixMs == clientTimeUnixMs && serverTimeUnixMs >= clientTimeUnixMs &&
								   shardCount == options.expectedShardCount && shardIndex == expectedShardIndex && contentInstanceId != 0 &&
								   workerThreadId != 0;
				if (!valid)
				{
					validationFailed = true;
				}

				const auto [it, inserted] = observations.emplace(userId, SShardObservation{shardIndex, contentInstanceId});
				if (!inserted && (it->second.shardIndex != shardIndex || it->second.contentInstanceId != contentInstanceId))
				{
					validationFailed = true;
				}

				++successCount;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				++failureCount;
				std::cerr << "[FAIL] RPC call error=" << static_cast<int>(failure.error)
						  << " remote=" << static_cast<int>(failure.remoteResponseCode) << '\n';
			},
			sequence,
			userId,
			clientTimeUnixMs);

		if (!callResult.accepted)
		{
			++failureCount;
			std::cerr << "[FAIL] RPC call start error=" << static_cast<int>(callResult.error) << '\n';
		}
	}

	const auto responseDeadline = std::chrono::steady_clock::now() + requestTimeout + std::chrono::seconds(1);
	while (successCount + failureCount < options.pingCount && !disconnected && std::chrono::steady_clock::now() < responseDeadline)
	{
		ClientNetworkLib::FClientEvent event;
		while (network.TryPopEvent(event))
		{
			if (event.Type == ClientNetworkLib::EClientEventType::Disconnected ||
				event.Type == ClientNetworkLib::EClientEventType::SessionError ||
				event.Type == ClientNetworkLib::EClientEventType::SendFailed)
			{
				disconnected = true;
				error = event.Message;
				break;
			}

			if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived ||
				event.Packet.Opcode != static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response))
			{
				continue;
			}

			RpcLib::Protocol::FRpcResponse response;
			if (!RpcLib::Protocol::DeserializeRpcResponse(event.Packet.Payload, response))
			{
				validationFailed = true;
				continue;
			}

			const auto completionResult = rpcCommon.ProcessResponse(sessionId, response);
			if (completionResult != RpcLib::Protocol::ERpcCompletionResult::Completed &&
				completionResult != RpcLib::Protocol::ERpcCompletionResult::RemoteError)
			{
				validationFailed = true;
			}
		}

		rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
		if (successCount + failureCount < options.pingCount)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	if (disconnected || validationFailed || failureCount != 0 || successCount != options.pingCount)
	{
		std::cerr << "[FAIL] Cache RPC Ping success=" << successCount << " failure=" << failureCount << " disconnected=" << disconnected
				  << " validationFailed=" << validationFailed;
		if (!error.empty())
		{
			std::cerr << " error=" << error;
		}
		std::cerr << '\n';
		network.Stop();
		return 1;
	}

	std::cout << "[PASS] Cache RPC Ping count=" << successCount << " shards=" << options.expectedShardCount
			  << " observedUsers=" << observations.size() << '\n';
	for (const auto& [userId, observation] : observations)
	{
		std::cout << "  userId=" << userId << " shard=" << observation.shardIndex << " contentInstanceId=" << observation.contentInstanceId
				  << '\n';
	}

	if (options.loadUserId != 0)
	{
		const std::uint64_t loadRoutingKey = options.loadRoutingKey == 0 ? options.loadUserId : options.loadRoutingKey;
		const std::uint32_t expectedShardIndex = static_cast<std::uint32_t>(loadRoutingKey % options.expectedShardCount);
		auto executeLoadCall = [&](const std::uint64_t sequence,
								   const Cache::Protocol::ECacheUserLoadResult expectedResult,
								   const std::uint8_t expectedLoadedFromDatabase,
								   SLoadObservation& outObservation) -> bool
		{
			bool responseReceived = false;
			bool callFailed = false;
			bool responseInvalid = false;

			RpcLib::Protocol::FRpcTarget target;
			target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
			target.serverInstanceId = options.cacheInstanceId;
			target.routingKey = loadRoutingKey;
			const auto callResult = rpcCommon.Call<Cache::Protocol::FLoadCacheUserRpc>(
				target,
				requestTimeout,
				[&](const std::uint64_t responseSequence,
					const std::uint64_t responseUserId,
					const Cache::Protocol::ECacheUserLoadResult result,
					const std::uint8_t loadedFromDatabase,
					const std::uint32_t shardIndex,
					const std::uint32_t shardCount,
					const std::uint64_t contentInstanceId,
					const std::uint32_t currencyCount,
					const std::uint32_t inventoryCount,
					const std::uint64_t loadedAtUnixMs)
				{
					outObservation = SLoadObservation{shardIndex, contentInstanceId, currencyCount, inventoryCount, loadedAtUnixMs};
					responseInvalid = responseSequence != sequence || responseUserId != options.loadUserId || result != expectedResult ||
									  loadedFromDatabase != expectedLoadedFromDatabase || shardIndex != expectedShardIndex ||
									  shardCount != options.expectedShardCount || contentInstanceId == 0;
					if (expectedResult == Cache::Protocol::ECacheUserLoadResult::Success)
					{
						responseInvalid = responseInvalid || currencyCount != options.expectedCurrencyCount ||
										  inventoryCount != options.expectedInventoryCount || loadedAtUnixMs == 0;
					}
					else
					{
						responseInvalid = responseInvalid || currencyCount != 0 || inventoryCount != 0 || loadedAtUnixMs != 0;
					}
					responseReceived = true;
				},
				[&](const RpcLib::Protocol::FRpcCallFailure& failure)
				{
					callFailed = true;
					std::cerr << "[FAIL] Cache User Load RPC error=" << static_cast<int>(failure.error)
							  << " remote=" << static_cast<int>(failure.remoteResponseCode) << '\n';
				},
				sequence,
				options.loadUserId);

			if (!callResult.accepted)
			{
				std::cerr << "[FAIL] Cache User Load start error=" << static_cast<int>(callResult.error) << '\n';
				return false;
			}

			const auto deadline = std::chrono::steady_clock::now() + requestTimeout + std::chrono::seconds(1);
			while (!responseReceived && !callFailed && !disconnected && std::chrono::steady_clock::now() < deadline)
			{
				ClientNetworkLib::FClientEvent event;
				while (network.TryPopEvent(event))
				{
					if (event.Type == ClientNetworkLib::EClientEventType::Disconnected ||
						event.Type == ClientNetworkLib::EClientEventType::SessionError ||
						event.Type == ClientNetworkLib::EClientEventType::SendFailed)
					{
						disconnected = true;
						error = event.Message;
						break;
					}

					if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived ||
						event.Packet.Opcode != static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response))
					{
						continue;
					}

					RpcLib::Protocol::FRpcResponse response;
					if (!RpcLib::Protocol::DeserializeRpcResponse(event.Packet.Payload, response))
					{
						responseInvalid = true;
						continue;
					}

					const auto completionResult = rpcCommon.ProcessResponse(sessionId, response);
					if (completionResult != RpcLib::Protocol::ERpcCompletionResult::Completed &&
						completionResult != RpcLib::Protocol::ERpcCompletionResult::RemoteError)
					{
						responseInvalid = true;
					}
				}

				rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
				if (!responseReceived && !callFailed)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			if (!responseReceived || callFailed || disconnected || responseInvalid)
			{
				std::cerr << "[FAIL] Cache User Load sequence=" << sequence << " responseReceived=" << responseReceived
						  << " callFailed=" << callFailed << " disconnected=" << disconnected << " validationFailed=" << responseInvalid;
				if (!error.empty())
				{
					std::cerr << " error=" << error;
				}
				std::cerr << '\n';
				return false;
			}

			return true;
		};

		SLoadObservation firstLoad;
		const std::uint64_t firstSequence = static_cast<std::uint64_t>(options.pingCount) + 1;
		if (options.expectInvalidLoad)
		{
			RpcLib::Protocol::FRpcTarget invalidTarget;
			invalidTarget.serverType = RpcLib::Protocol::ERpcServerType::Cache;
			invalidTarget.serverInstanceId = options.cacheInstanceId;
			invalidTarget.routingKey = loadRoutingKey;
			const std::size_t pendingCountBeforeInvalidCall = rpcCommon.GetPendingCallCount();
			const auto invalidCall = rpcCommon.Call<Cache::Protocol::FLoadCacheUserRpc>(
				invalidTarget,
				requestTimeout,
				[](auto&&...) {},
				[](const RpcLib::Protocol::FRpcCallFailure&) {},
				firstSequence,
				options.loadUserId);
			if (invalidCall.accepted || invalidCall.error != RpcLib::Protocol::ERpcCallError::InvalidArgument ||
				rpcCommon.GetPendingCallCount() != pendingCountBeforeInvalidCall)
			{
				std::cerr << "[FAIL] Cache User Load routing mismatch was not rejected before send. error="
						  << static_cast<int>(invalidCall.error) << '\n';
				network.Stop();
				return 1;
			}
			std::cout << "[PASS] Cache User Load invalid route rejected before send. routingKey=" << loadRoutingKey
					  << " userId=" << options.loadUserId << '\n';

			RpcLib::Protocol::FRpcRequest malformedRequest;
			malformedRequest.requestId = std::numeric_limits<std::uint64_t>::max() - firstSequence;
			malformedRequest.serviceId = Cache::Protocol::FLoadCacheUserRpc::kServiceId;
			malformedRequest.methodId = Cache::Protocol::FLoadCacheUserRpc::kMethodId;
			malformedRequest.routingKey = loadRoutingKey;
			malformedRequest.originContentInstanceId = 1;
			{
				NetworkLib::Packet::Serialization::FPacketWriter writer;
				if (!RpcLib::Protocol::WriteRpcArguments(
						writer, Cache::Protocol::FLoadCacheUserRpc::FRequestArguments{firstSequence, options.loadUserId}))
				{
					std::cerr << "[FAIL] malformed Cache User Load serialization failed.\n";
					network.Stop();
					return 1;
				}
				malformedRequest.payload = writer.MoveBuffer();
			}

			if (!transport.SendRequest(sessionId, malformedRequest))
			{
				std::cerr << "[FAIL] malformed Cache User Load send failed. error=" << transport.GetLastError() << '\n';
				network.Stop();
				return 1;
			}

			bool malformedResponseReceived = false;
			bool malformedResponseRejected = false;
			const auto malformedDeadline = std::chrono::steady_clock::now() + requestTimeout + std::chrono::seconds(1);
			while (!malformedResponseReceived && std::chrono::steady_clock::now() < malformedDeadline)
			{
				ClientNetworkLib::FClientEvent event;
				while (network.TryPopEvent(event))
				{
					if (event.Type == ClientNetworkLib::EClientEventType::Disconnected ||
						event.Type == ClientNetworkLib::EClientEventType::SessionError ||
						event.Type == ClientNetworkLib::EClientEventType::SendFailed)
					{
						std::cerr << "[FAIL] malformed Cache User Load connection failed. error=" << event.Message << '\n';
						network.Stop();
						return 1;
					}

					if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived ||
						event.Packet.Opcode != static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response))
					{
						continue;
					}

					RpcLib::Protocol::FRpcResponse response;
					if (!RpcLib::Protocol::DeserializeRpcResponse(event.Packet.Payload, response) ||
						response.requestId != malformedRequest.requestId)
					{
						continue;
					}

					malformedResponseReceived = true;
					malformedResponseRejected = response.resultCode == RpcLib::Protocol::ERpcResponseCode::InvalidPayload;
					break;
				}

				if (!malformedResponseReceived)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			if (!malformedResponseReceived || !malformedResponseRejected)
			{
				std::cerr << "[FAIL] malformed Cache User Load was not rejected by the receiver.\n";
				network.Stop();
				return 1;
			}
			std::cout << "[PASS] Cache User Load invalid route rejected by receiver. routingKey=" << loadRoutingKey
					  << " userId=" << options.loadUserId << '\n';
		}
		else
		{
			SLoadObservation secondLoad;
			if (!executeLoadCall(firstSequence, Cache::Protocol::ECacheUserLoadResult::Success, 1, firstLoad) ||
				!executeLoadCall(firstSequence + 1, Cache::Protocol::ECacheUserLoadResult::Success, 0, secondLoad) ||
				firstLoad.shardIndex != secondLoad.shardIndex || firstLoad.contentInstanceId != secondLoad.contentInstanceId ||
				firstLoad.currencyCount != secondLoad.currencyCount || firstLoad.inventoryCount != secondLoad.inventoryCount ||
				firstLoad.loadedAtUnixMs != secondLoad.loadedAtUnixMs)
			{
				std::cerr << "[FAIL] Cache User Load miss/hit observations do not match.\n";
				network.Stop();
				return 1;
			}

			std::cout << "[PASS] Cache User Load userId=" << options.loadUserId << " first=database second=cache"
					  << " shard=" << firstLoad.shardIndex << " contentInstanceId=" << firstLoad.contentInstanceId
					  << " currencies=" << firstLoad.currencyCount << " inventory=" << firstLoad.inventoryCount << '\n';
		}
	}

	network.DisconnectSession(sessionId, "cache RPC test completed.");
	network.Stop();
	return 0;
}
