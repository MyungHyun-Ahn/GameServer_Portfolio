#include "CacheRpcPingClientPch.h"

#include "CacheRpcPingClient/UserPresenceSmoke.h"

namespace
{
	constexpr std::uint8_t kPacketKey = 0x37;
	constexpr std::uint8_t kRandomKey = 0x51;
	constexpr std::uint64_t kMutationSmokeExperience = 100;
	constexpr std::uint32_t kMutationSmokeStatPointReward = 5;
	constexpr std::uint32_t kMutationSmokeAllocatedStr = 1;
	constexpr std::uint32_t kFirstEquipmentItemDataId = 1001;
	constexpr std::uint64_t kFirstEquipmentVersionAfterEquip = 2;
	constexpr std::uint32_t kFirstEquipmentStr = 3;
	constexpr std::uint32_t kSecondEquipmentItemDataId = 1002;
	constexpr std::uint64_t kSecondEquipmentVersionAfterEquip = 6;
	constexpr std::uint32_t kSecondEquipmentDex = 4;

	bool IsSingleEquippedItem(
		const Cache::Protocol::FPlayerWorldSnapshot& snapshot,
		const std::uint64_t expectedItemInstanceId,
		const std::uint32_t expectedItemDataId,
		const std::uint64_t expectedItemVersion,
		const std::uint32_t expectedStr,
		const std::uint32_t expectedDex,
		const std::uint32_t expectedInt,
		const std::uint32_t expectedLuk)
	{
		if (snapshot.equippedItems.size() != 1)
		{
			return false;
		}

		const Cache::Protocol::FEquippedItemSnapshot& item = snapshot.equippedItems.front();
		return item.itemInstanceId == expectedItemInstanceId && item.itemDataId == expectedItemDataId &&
			   item.itemVersion == expectedItemVersion && item.strStat == expectedStr && item.dexStat == expectedDex &&
			   item.intStat == expectedInt && item.lukStat == expectedLuk;
	}

	struct SOptions final
	{
		std::string serverIp = "127.0.0.1";
		std::uint16_t port = 19103;
		std::uint64_t userId = 880002;
		std::uint64_t firstLocalClientSessionId = 710001;
		std::uint64_t secondLocalClientSessionId = 720001;
		RpcLib::Protocol::FRpcServerInstanceId firstGameInstanceId = 1;
		RpcLib::Protocol::FRpcServerInstanceId secondGameInstanceId = 2;
		RpcLib::Protocol::FRpcServerInstanceId cacheInstanceId = 1;
		std::uint32_t timeoutMilliseconds = 5000;
	};

	struct SCallState final
	{
		bool completed = false;
		bool failed = false;
		bool responseValid = false;
		RpcLib::Protocol::FRpcCallFailure failure{};
	};

	struct SRevokeObservation final
	{
		std::uint64_t userId = 0;
		std::uint64_t localClientSessionId = 0;
		std::uint64_t ownerGeneration = 0;
		ServerProtocol::UserPresence::ERevokeUserReason reason = ServerProtocol::UserPresence::ERevokeUserReason::Unknown;
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
			if (argument == "--user-presence-smoke")
			{
				continue;
			}
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
			else if (argument == "--user-id" && index + 1 < argc)
			{
				if (!TryParseUnsigned(argv[++index], outOptions.userId) || outOptions.userId == 0)
				{
					return false;
				}
			}
			else if (argument == "--first-local-session-id" && index + 1 < argc)
			{
				if (!TryParseUnsigned(argv[++index], outOptions.firstLocalClientSessionId) || outOptions.firstLocalClientSessionId == 0)
				{
					return false;
				}
			}
			else if (argument == "--second-local-session-id" && index + 1 < argc)
			{
				if (!TryParseUnsigned(argv[++index], outOptions.secondLocalClientSessionId) || outOptions.secondLocalClientSessionId == 0)
				{
					return false;
				}
			}
			else if (argument == "--first-game-instance-id" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.firstGameInstanceId = static_cast<RpcLib::Protocol::FRpcServerInstanceId>(value);
			}
			else if (argument == "--second-game-instance-id" && index + 1 < argc)
			{
				std::uint64_t value = 0;
				if (!TryParseUnsigned(argv[++index], value) || value == 0 || value > std::numeric_limits<std::uint32_t>::max())
				{
					return false;
				}
				outOptions.secondGameInstanceId = static_cast<RpcLib::Protocol::FRpcServerInstanceId>(value);
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

		return !outOptions.serverIp.empty() && outOptions.firstGameInstanceId != outOptions.secondGameInstanceId &&
			   outOptions.firstLocalClientSessionId != outOptions.secondLocalClientSessionId;
	}

	ClientNetworkLib::FClientNetworkConfig BuildNetworkConfig(
		const SOptions& options)
	{
		ClientNetworkLib::FClientNetworkConfig config{};
		config.ServerIp = options.serverIp;
		config.ServerPort = options.port;
		config.WorkerThreadCount = 2;
		config.RecvScratchBufferSize = 64 * 1024;
		config.PacketCipherConfig.packetKey = kPacketKey;
		return config;
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

	class FGamePeer final
	{
	public:
		FGamePeer(
			const SOptions& options,
			const RpcLib::Protocol::FRpcServerInstanceId gameInstanceId,
			const std::uint64_t localClientSessionId)
			: m_cacheInstanceId(options.cacheInstanceId)
			, m_gameInstanceId(gameInstanceId)
			, m_localClientSessionId(localClientSessionId)
			, m_timeout(std::chrono::milliseconds(options.timeoutMilliseconds))
			, m_network(BuildNetworkConfig(options))
			, m_transport(m_network)
			, m_rpcCommon(m_sessionRegistry, m_dispatcher, m_requestIdGenerator, m_transport, 1000ULL + gameInstanceId, 32)
		{
			const bool registered = m_rpcCommon.Register<ServerProtocol::UserPresence::FRevokeUserRpc>(
				[this](const RpcLib::Dispatch::FRpcCallContext& context,
					RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRevokeUserRpc>& reply,
					const std::uint64_t userId,
					const std::uint64_t localClientSessionId,
					const std::uint64_t ownerGeneration,
					const ServerProtocol::UserPresence::ERevokeUserReason reason)
				{
					HandleRevoke(context, reply, userId, localClientSessionId, ownerGeneration, reason);
				});
			if (!registered)
			{
				throw std::runtime_error("RevokeUser RPC registration failed.");
			}
		}

		~FGamePeer()
		{
			Stop();
		}

		FGamePeer(const FGamePeer&) = delete;
		FGamePeer& operator=(const FGamePeer&) = delete;

		bool Connect()
		{
			if (!m_network.Start(m_error))
			{
				return false;
			}

			if (!m_network.ConnectSession(m_sessionId, m_error))
			{
				return false;
			}

			m_transport.SetSessionId(m_sessionId);
			if (!m_sessionRegistry.Add(m_sessionId))
			{
				m_error = "RPC session registration failed.";
				return false;
			}

			const std::shared_ptr<RpcLib::Session::FRpcSession> rpcSession = m_sessionRegistry.Find(m_sessionId);
			if (rpcSession == nullptr || !rpcSession->BeginHandshake())
			{
				m_error = "RPC handshake state transition failed.";
				return false;
			}

			RpcLib::Protocol::FRpcHelloRequest helloRequest;
			helloRequest.serverType = RpcLib::Protocol::ERpcServerType::Game;
			helloRequest.serverInstanceId = m_gameInstanceId;
			if (!m_transport.SendHelloRequest(helloRequest))
			{
				m_error = m_transport.GetLastError();
				return false;
			}

			const auto deadline = std::chrono::steady_clock::now() + m_timeout;
			while (!m_ready && !m_failed && std::chrono::steady_clock::now() < deadline)
			{
				Pump();
				if (!m_ready && !m_failed)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			if (!m_ready && !m_failed)
			{
				m_failed = true;
				m_error = "RPC Hello timed out.";
			}
			return m_ready && !m_failed;
		}

		void Stop() noexcept
		{
			if (m_sessionId != 0)
			{
				m_network.DisconnectSession(m_sessionId, "UserPresence smoke completed.");
				m_sessionRegistry.Remove(m_sessionId);
				m_sessionId = 0;
			}
			m_network.Stop();
			m_ready = false;
		}

		void Pump()
		{
			ClientNetworkLib::FClientEvent event;
			while (m_network.TryPopEvent(event))
			{
				HandleEvent(event);
			}

			m_rpcCommon.ProcessTimeouts(std::chrono::steady_clock::now());
		}

		RpcLib::FRpcCommon& GetRpcCommon() noexcept
		{
			return m_rpcCommon;
		}

		RpcLib::Protocol::FRpcTarget BuildCacheTarget(
			const std::uint64_t userId) const noexcept
		{
			RpcLib::Protocol::FRpcTarget target;
			target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
			target.serverInstanceId = m_cacheInstanceId;
			target.routingKey = userId;
			return target;
		}

		void SetOwnedUser(
			const std::uint64_t userId,
			const std::uint64_t ownerGeneration) noexcept
		{
			m_ownedUserId = userId;
			m_ownerGeneration = ownerGeneration;
			m_hasOwnedUser = true;
		}

		std::uint64_t GetLocalClientSessionId() const noexcept
		{
			return m_localClientSessionId;
		}

		RpcLib::Protocol::FRpcServerInstanceId GetGameInstanceId() const noexcept
		{
			return m_gameInstanceId;
		}

		bool HasFailed() const noexcept
		{
			return m_failed;
		}

		const std::string& GetError() const noexcept
		{
			return m_error;
		}

		std::size_t GetSuccessfulRevokeCount() const noexcept
		{
			return m_successfulRevokeCount;
		}

		const SRevokeObservation& GetLastRevoke() const noexcept
		{
			return m_lastRevoke;
		}

	private:
		void HandleEvent(
			const ClientNetworkLib::FClientEvent& event)
		{
			if (event.Type == ClientNetworkLib::EClientEventType::Disconnected ||
				event.Type == ClientNetworkLib::EClientEventType::SessionError ||
				event.Type == ClientNetworkLib::EClientEventType::SendFailed ||
				event.Type == ClientNetworkLib::EClientEventType::ConnectFailed)
			{
				m_failed = true;
				m_error = event.Message.empty() ? "client network failure." : event.Message;
				return;
			}

			if (event.Type != ClientNetworkLib::EClientEventType::PacketReceived)
			{
				return;
			}

			const auto opcode = static_cast<RpcLib::Protocol::ERpcWireOpcode>(event.Packet.Opcode);
			switch (opcode)
			{
				case RpcLib::Protocol::ERpcWireOpcode::HelloResponse:
					HandleHelloResponse(event.Packet.Payload);
					break;
				case RpcLib::Protocol::ERpcWireOpcode::Request:
					HandleRequest(event.Packet.Payload);
					break;
				case RpcLib::Protocol::ERpcWireOpcode::Response:
					HandleResponse(event.Packet.Payload);
					break;
				default:
					m_failed = true;
					m_error = "unexpected RPC wire opcode.";
					break;
			}
		}

		void HandleHelloResponse(
			const std::vector<char>& payload)
		{
			RpcLib::Protocol::FRpcHelloResponse response;
			if (m_ready || !RpcLib::Protocol::DeserializeRpcHelloResponse(payload, response) ||
				response.result != RpcLib::Protocol::ERpcHelloResult::Success ||
				response.serverType != RpcLib::Protocol::ERpcServerType::Cache || response.serverInstanceId != m_cacheInstanceId ||
				!m_sessionRegistry.MarkReady(m_sessionId, response.serverType, response.serverInstanceId, response.protocolVersion))
			{
				m_failed = true;
				m_error = "invalid RPC Hello response.";
				return;
			}

			m_ready = true;
		}

		void HandleRequest(
			const std::vector<char>& payload)
		{
			RpcLib::Protocol::FRpcRequest request;
			if (!m_ready || !RpcLib::Protocol::DeserializeRpcRequest(payload, request))
			{
				m_failed = true;
				m_error = "invalid incoming RPC request.";
				return;
			}

			const RpcLib::Protocol::FRpcResponse response = m_rpcCommon.DispatchRequest(m_sessionId, request);
			if (!m_transport.SendResponse(m_sessionId, response))
			{
				m_failed = true;
				m_error = m_transport.GetLastError();
			}
		}

		void HandleResponse(
			const std::vector<char>& payload)
		{
			RpcLib::Protocol::FRpcResponse response;
			if (!m_ready || !RpcLib::Protocol::DeserializeRpcResponse(payload, response))
			{
				m_failed = true;
				m_error = "invalid incoming RPC response.";
				return;
			}

			const RpcLib::Protocol::ERpcCompletionResult result = m_rpcCommon.ProcessResponse(m_sessionId, response);
			if (result != RpcLib::Protocol::ERpcCompletionResult::Completed &&
				result != RpcLib::Protocol::ERpcCompletionResult::RemoteError)
			{
				m_failed = true;
				m_error = "RPC response did not match a pending call.";
			}
		}

		void HandleRevoke(
			const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRevokeUserRpc>& reply,
			const std::uint64_t userId,
			const std::uint64_t localClientSessionId,
			const std::uint64_t ownerGeneration,
			const ServerProtocol::UserPresence::ERevokeUserReason reason)
		{
			m_lastRevoke = {userId, localClientSessionId, ownerGeneration, reason};

			ServerProtocol::UserPresence::ERevokeUserResult result = ServerProtocol::UserPresence::ERevokeUserResult::Revoked;
			if (context.rpcSessionId != m_sessionId || context.peerServerType != RpcLib::Protocol::ERpcServerType::Cache ||
				context.peerServerInstanceId != m_cacheInstanceId)
			{
				result = ServerProtocol::UserPresence::ERevokeUserResult::UnauthorizedCaller;
			}
			else if (userId == 0 || localClientSessionId == 0 || ownerGeneration == 0 || context.routingKey != userId ||
					 reason == ServerProtocol::UserPresence::ERevokeUserReason::Unknown)
			{
				result = ServerProtocol::UserPresence::ERevokeUserResult::InvalidRequest;
			}
			else if (!m_hasOwnedUser || m_ownedUserId != userId)
			{
				result = ServerProtocol::UserPresence::ERevokeUserResult::UserNotFound;
			}
			else if (m_localClientSessionId != localClientSessionId)
			{
				result = ServerProtocol::UserPresence::ERevokeUserResult::SessionMismatch;
			}
			else if (m_ownerGeneration != ownerGeneration)
			{
				result = ServerProtocol::UserPresence::ERevokeUserResult::StaleOwner;
			}

			if (result == ServerProtocol::UserPresence::ERevokeUserResult::Revoked)
			{
				m_hasOwnedUser = false;
				++m_successfulRevokeCount;
			}

			reply.Send(result);
		}

	private:
		RpcLib::Protocol::FRpcServerInstanceId m_cacheInstanceId = 0;
		RpcLib::Protocol::FRpcServerInstanceId m_gameInstanceId = 0;
		std::uint64_t m_localClientSessionId = 0;
		std::chrono::milliseconds m_timeout{0};
		ClientNetworkLib::FClientNetwork m_network;
		FClientRpcTransport m_transport;
		RpcLib::Session::FRpcSessionRegistry m_sessionRegistry;
		RpcLib::Call::FRpcRequestIdGenerator m_requestIdGenerator;
		RpcLib::Dispatch::FRpcMethodDispatcher m_dispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
		ClientNetworkLib::FClientSessionId m_sessionId = 0;
		bool m_ready = false;
		bool m_failed = false;
		std::string m_error;
		bool m_hasOwnedUser = false;
		std::uint64_t m_ownedUserId = 0;
		std::uint64_t m_ownerGeneration = 0;
		std::size_t m_successfulRevokeCount = 0;
		SRevokeObservation m_lastRevoke;
	};

	template <typename TPredicate>
	bool PumpUntil(
		FGamePeer& firstPeer,
		FGamePeer& secondPeer,
		const std::chrono::steady_clock::time_point deadline,
		TPredicate&& predicate)
	{
		while (!firstPeer.HasFailed() && !secondPeer.HasFailed() && std::chrono::steady_clock::now() < deadline)
		{
			firstPeer.Pump();
			secondPeer.Pump();
			if (predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return predicate();
	}

	void SetCallFailure(
		SCallState& state,
		const RpcLib::Protocol::FRpcCallFailure& failure)
	{
		state.failed = true;
		state.failure = failure;
	}

	bool ValidateCall(
		const char* operation,
		const RpcLib::Protocol::FRpcCallStartResult& startResult,
		const SCallState& state,
		FGamePeer& firstPeer,
		FGamePeer& secondPeer,
		const std::chrono::milliseconds timeout)
	{
		if (!startResult.accepted)
		{
			std::cerr << "[FAIL] " << operation << " start error=" << static_cast<int>(startResult.error) << '\n';
			return false;
		}

		const auto deadline = std::chrono::steady_clock::now() + timeout + std::chrono::seconds(1);
		if (!PumpUntil(firstPeer,
				secondPeer,
				deadline,
				[&state]()
				{
					return state.completed || state.failed;
				}))
		{
			std::cerr << "[FAIL] " << operation << " timed out." << '\n';
			return false;
		}

		if (firstPeer.HasFailed() || secondPeer.HasFailed())
		{
			std::cerr << "[FAIL] " << operation << " peer failure. first=" << firstPeer.GetError() << " second=" << secondPeer.GetError()
					  << '\n';
			return false;
		}

		if (state.failed)
		{
			std::cerr << "[FAIL] " << operation << " RPC failure=" << static_cast<int>(state.failure.error)
					  << " remote=" << static_cast<int>(state.failure.remoteResponseCode) << '\n';
			return false;
		}

		if (!state.completed || !state.responseValid)
		{
			std::cerr << "[FAIL] " << operation << " returned an unexpected result." << '\n';
			return false;
		}

		return true;
	}
}

int CacheRpcPingClient::RunUserPresenceSmoke(
	const int argc,
	char* argv[])
{
	SOptions options;
	if (!TryParseOptions(argc, argv, options))
	{
		std::cerr << "Usage: CacheRpcPingClient --user-presence-smoke [--host IP] [--port N] [--user-id N]"
				  << " [--first-game-instance-id N] [--second-game-instance-id N]"
				  << " [--first-local-session-id N] [--second-local-session-id N]"
				  << " [--cache-instance-id N] [--timeout-ms N]\n";
		return 1;
	}

	try
	{
		if (options.userId > (std::numeric_limits<std::uint64_t>::max() - 3) / 10)
		{
			std::cerr << "[FAIL] userId is too large for equipment smoke item IDs.\n";
			return 1;
		}
		const std::uint64_t firstEquipmentItemInstanceId = options.userId * 10 + 1;
		const std::uint64_t secondEquipmentItemInstanceId = options.userId * 10 + 2;
		const std::uint64_t consumableItemInstanceId = options.userId * 10 + 3;

		FGamePeer firstPeer(options, options.firstGameInstanceId, options.firstLocalClientSessionId);
		FGamePeer secondPeer(options, options.secondGameInstanceId, options.secondLocalClientSessionId);
		if (!firstPeer.Connect() || !secondPeer.Connect())
		{
			std::cerr << "[FAIL] Game peer connection. first=" << firstPeer.GetError() << " second=" << secondPeer.GetError() << '\n';
			return 1;
		}

		const auto timeout = std::chrono::milliseconds(options.timeoutMilliseconds);
		SCallState preEnterSnapshot;
		const auto preEnterSnapshotStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot&)
			{
				preEnterSnapshot.responseValid = result == Cache::Protocol::EPlayerProgressResult::UnauthorizedCaller;
				preEnterSnapshot.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(preEnterSnapshot, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			1ULL);
		if (!ValidateCall(
				"GetPlayerWorldSnapshot before EnterUser", preEnterSnapshotStart, preEnterSnapshot, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		std::uint64_t firstGeneration = 0;
		std::uint32_t leaseDurationMilliseconds = 0;
		SCallState firstEnter;
		const auto firstEnterStart = firstPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FEnterUserRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const std::uint64_t userId,
				const ServerProtocol::UserPresence::EEnterUserResult result,
				const std::uint64_t ownerGeneration,
				const std::uint32_t responseLeaseDurationMilliseconds)
			{
				firstGeneration = ownerGeneration;
				leaseDurationMilliseconds = responseLeaseDurationMilliseconds;
				firstEnter.responseValid = userId == options.userId && result == ServerProtocol::UserPresence::EEnterUserResult::Entered &&
										   ownerGeneration != 0 && responseLeaseDurationMilliseconds != 0;
				if (firstEnter.responseValid)
				{
					firstPeer.SetOwnedUser(userId, ownerGeneration);
				}
				firstEnter.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(firstEnter, failure);
			},
			options.userId,
			options.firstLocalClientSessionId);
		if (!ValidateCall("Game #1 EnterUser", firstEnterStart, firstEnter, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot initialSnapshot;
		SCallState ownedSnapshot;
		const auto ownedSnapshotStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot& snapshot)
			{
				initialSnapshot = snapshot;
				ownedSnapshot.responseValid =
					result == Cache::Protocol::EPlayerProgressResult::Success && snapshot.progress.characterId != 0 &&
					snapshot.progress.level == 1 && snapshot.progress.exp == 0 && snapshot.progress.unspentStatPoints == 0 &&
					snapshot.progress.progressVersion != 0 && snapshot.progress.statVersion != 0 && snapshot.equippedItems.empty() &&
					snapshot.equipmentVersion != 0 && snapshot.statRevision != 0;
				ownedSnapshot.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(ownedSnapshot, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration);
		if (!ValidateCall("GetPlayerWorldSnapshot after EnterUser", ownedSnapshotStart, ownedSnapshot, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot experienceSnapshot;
		SCallState grantExperience;
		const auto grantExperienceStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FGrantPlayerExperienceRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot& snapshot)
			{
				experienceSnapshot = snapshot;
				grantExperience.responseValid =
					result == Cache::Protocol::EPlayerProgressResult::Success &&
					snapshot.progress.characterId == initialSnapshot.progress.characterId &&
					snapshot.progress.characterDataId == initialSnapshot.progress.characterDataId &&
					snapshot.progress.level == initialSnapshot.progress.level + 1 && snapshot.progress.exp == 0 &&
					snapshot.progress.strStat == initialSnapshot.progress.strStat &&
					snapshot.progress.dexStat == initialSnapshot.progress.dexStat &&
					snapshot.progress.intStat == initialSnapshot.progress.intStat &&
					snapshot.progress.lukStat == initialSnapshot.progress.lukStat &&
					snapshot.progress.unspentStatPoints == initialSnapshot.progress.unspentStatPoints + kMutationSmokeStatPointReward &&
					snapshot.progress.progressVersion == initialSnapshot.progress.progressVersion + 1 &&
					snapshot.progress.statVersion == initialSnapshot.progress.statVersion + 1 && snapshot.equippedItems.empty() &&
					snapshot.equipmentVersion == initialSnapshot.equipmentVersion &&
					snapshot.statRevision == initialSnapshot.statRevision + 1;
				grantExperience.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(grantExperience, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			initialSnapshot.progress.progressVersion,
			kMutationSmokeExperience);
		if (!ValidateCall("current owner GrantPlayerExperience", grantExperienceStart, grantExperience, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot allocationSnapshot;
		SCallState allocateStat;
		const auto allocateStatStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FAllocatePlayerStatRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot& snapshot)
			{
				allocationSnapshot = snapshot;
				allocateStat.responseValid =
					result == Cache::Protocol::EPlayerProgressResult::Success &&
					snapshot.progress.characterId == experienceSnapshot.progress.characterId &&
					snapshot.progress.characterDataId == experienceSnapshot.progress.characterDataId &&
					snapshot.progress.level == experienceSnapshot.progress.level &&
					snapshot.progress.exp == experienceSnapshot.progress.exp &&
					snapshot.progress.strStat == experienceSnapshot.progress.strStat + kMutationSmokeAllocatedStr &&
					snapshot.progress.dexStat == experienceSnapshot.progress.dexStat &&
					snapshot.progress.intStat == experienceSnapshot.progress.intStat &&
					snapshot.progress.lukStat == experienceSnapshot.progress.lukStat &&
					snapshot.progress.unspentStatPoints == experienceSnapshot.progress.unspentStatPoints - kMutationSmokeAllocatedStr &&
					snapshot.progress.progressVersion == experienceSnapshot.progress.progressVersion &&
					snapshot.progress.statVersion == experienceSnapshot.progress.statVersion + 1 && snapshot.equippedItems.empty() &&
					snapshot.equipmentVersion == experienceSnapshot.equipmentVersion &&
					snapshot.statRevision == experienceSnapshot.statRevision + 1;
				allocateStat.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(allocateStat, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			experienceSnapshot.progress.statVersion,
			kMutationSmokeAllocatedStr,
			0U,
			0U,
			0U);
		if (!ValidateCall("current owner AllocatePlayerStat", allocateStatStart, allocateStat, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState rejectConsumable;
		const auto rejectConsumableStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FEquipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot&,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				rejectConsumable.responseValid =
					result == Cache::Protocol::EPlayerEquipmentResult::NotEquipment && itemVersion == 0 && equipped;
				rejectConsumable.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(rejectConsumable, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			consumableItemInstanceId,
			1ULL,
			allocationSnapshot.statRevision,
			allocationSnapshot.equipmentVersion);
		if (!ValidateCall("reject non-equipment item", rejectConsumableStart, rejectConsumable, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot firstEquipmentSnapshot;
		SCallState equipFirstItem;
		const auto equipFirstItemStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FEquipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot& snapshot,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				firstEquipmentSnapshot = snapshot;
				equipFirstItem.responseValid = result == Cache::Protocol::EPlayerEquipmentResult::Success && equipped &&
											   itemVersion == kFirstEquipmentVersionAfterEquip &&
											   snapshot.progress.statVersion == allocationSnapshot.progress.statVersion &&
											   IsSingleEquippedItem(snapshot,
												   firstEquipmentItemInstanceId,
												   kFirstEquipmentItemDataId,
												   kFirstEquipmentVersionAfterEquip,
												   kFirstEquipmentStr,
												   0,
												   0,
												   0) &&
											   snapshot.equipmentVersion != allocationSnapshot.equipmentVersion &&
											   snapshot.statRevision == allocationSnapshot.statRevision + 1;
				equipFirstItem.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(equipFirstItem, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			firstEquipmentItemInstanceId,
			1ULL,
			allocationSnapshot.statRevision,
			allocationSnapshot.equipmentVersion);
		if (!ValidateCall("equip first weapon", equipFirstItemStart, equipFirstItem, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState rejectStaleRevision;
		const auto rejectStaleRevisionStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FEquipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot&,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				rejectStaleRevision.responseValid =
					result == Cache::Protocol::EPlayerEquipmentResult::ConcurrentModification && itemVersion == 0 && equipped;
				rejectStaleRevision.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(rejectStaleRevision, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			secondEquipmentItemInstanceId,
			5ULL,
			allocationSnapshot.statRevision,
			allocationSnapshot.equipmentVersion);
		if (!ValidateCall("reject stale equipment revision", rejectStaleRevisionStart, rejectStaleRevision, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot replacementSnapshot;
		SCallState replaceEquipment;
		const auto replaceEquipmentStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FEquipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot& snapshot,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				replacementSnapshot = snapshot;
				replaceEquipment.responseValid = result == Cache::Protocol::EPlayerEquipmentResult::Success && equipped &&
												 itemVersion == kSecondEquipmentVersionAfterEquip &&
												 IsSingleEquippedItem(snapshot,
													 secondEquipmentItemInstanceId,
													 kSecondEquipmentItemDataId,
													 kSecondEquipmentVersionAfterEquip,
													 0,
													 kSecondEquipmentDex,
													 0,
													 0) &&
												 snapshot.equipmentVersion != firstEquipmentSnapshot.equipmentVersion &&
												 snapshot.statRevision == firstEquipmentSnapshot.statRevision + 1;
				replaceEquipment.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(replaceEquipment, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			secondEquipmentItemInstanceId,
			5ULL,
			firstEquipmentSnapshot.statRevision,
			firstEquipmentSnapshot.equipmentVersion);
		if (!ValidateCall("atomically replace same-slot weapon", replaceEquipmentStart, replaceEquipment, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState rejectStaleItemVersion;
		const auto rejectStaleItemVersionStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FUnequipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot&,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				rejectStaleItemVersion.responseValid =
					result == Cache::Protocol::EPlayerEquipmentResult::ItemVersionMismatch && itemVersion == 0 && !equipped;
				rejectStaleItemVersion.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(rejectStaleItemVersion, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			secondEquipmentItemInstanceId,
			5ULL,
			replacementSnapshot.statRevision,
			replacementSnapshot.equipmentVersion);
		if (!ValidateCall("reject stale item version", rejectStaleItemVersionStart, rejectStaleItemVersion, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		Cache::Protocol::FPlayerWorldSnapshot unequippedSnapshot;
		SCallState unequipItem;
		const auto unequipItemStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FUnequipPlayerItemRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerEquipmentResult result,
				const Cache::Protocol::FPlayerWorldSnapshot& snapshot,
				const std::uint64_t itemVersion,
				const bool equipped)
			{
				unequippedSnapshot = snapshot;
				unequipItem.responseValid =
					result == Cache::Protocol::EPlayerEquipmentResult::Success && itemVersion == kSecondEquipmentVersionAfterEquip + 1 &&
					!equipped && snapshot.equippedItems.empty() && snapshot.equipmentVersion == allocationSnapshot.equipmentVersion &&
					snapshot.statRevision == replacementSnapshot.statRevision + 1;
				unequipItem.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(unequipItem, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration,
			secondEquipmentItemInstanceId,
			6ULL,
			replacementSnapshot.statRevision,
			replacementSnapshot.equipmentVersion);
		if (!ValidateCall("explicitly unequip weapon", unequipItemStart, unequipItem, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		std::uint64_t secondGeneration = 0;
		SCallState secondEnter;
		const auto secondEnterStart = secondPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FEnterUserRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const std::uint64_t userId,
				const ServerProtocol::UserPresence::EEnterUserResult result,
				const std::uint64_t ownerGeneration,
				const std::uint32_t responseLeaseDurationMilliseconds)
			{
				secondGeneration = ownerGeneration;
				secondEnter.responseValid =
					userId == options.userId && result == ServerProtocol::UserPresence::EEnterUserResult::ReplacedPreviousGameServer &&
					ownerGeneration > firstGeneration && responseLeaseDurationMilliseconds == leaseDurationMilliseconds;
				if (secondEnter.responseValid)
				{
					secondPeer.SetOwnedUser(userId, ownerGeneration);
				}
				secondEnter.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(secondEnter, failure);
			},
			options.userId,
			options.secondLocalClientSessionId);
		if (!ValidateCall("Game #2 EnterUser", secondEnterStart, secondEnter, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		const auto revokeDeadline = std::chrono::steady_clock::now() + timeout + std::chrono::seconds(1);
		if (!PumpUntil(firstPeer,
				secondPeer,
				revokeDeadline,
				[&firstPeer]()
				{
					return firstPeer.GetSuccessfulRevokeCount() == 1;
				}))
		{
			std::cerr << "[FAIL] old owner did not receive RevokeUser." << '\n';
			return 1;
		}

		const SRevokeObservation& revoke = firstPeer.GetLastRevoke();
		if (revoke.userId != options.userId || revoke.localClientSessionId != options.firstLocalClientSessionId ||
			revoke.ownerGeneration != firstGeneration ||
			revoke.reason != ServerProtocol::UserPresence::ERevokeUserReason::ReplacedByNewLogin)
		{
			std::cerr << "[FAIL] RevokeUser payload does not match the old owner." << '\n';
			return 1;
		}

		SCallState replacedOwnerSnapshot;
		const auto replacedOwnerSnapshotStart = firstPeer.GetRpcCommon().Call<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot&)
			{
				replacedOwnerSnapshot.responseValid = result == Cache::Protocol::EPlayerProgressResult::UnauthorizedCaller;
				replacedOwnerSnapshot.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(replacedOwnerSnapshot, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration);
		if (!ValidateCall(
				"replaced owner GetPlayerWorldSnapshot", replacedOwnerSnapshotStart, replacedOwnerSnapshot, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState staleRenew;
		const auto staleRenewStart = firstPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FRenewUserRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const ServerProtocol::UserPresence::ERenewUserResult result)
			{
				staleRenew.responseValid = result == ServerProtocol::UserPresence::ERenewUserResult::StaleOwner;
				staleRenew.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(staleRenew, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration);
		if (!ValidateCall("old owner RenewUser", staleRenewStart, staleRenew, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState staleLeave;
		const auto staleLeaveStart = firstPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FLeaveUserRpc>(
			firstPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const ServerProtocol::UserPresence::ELeaveUserResult result)
			{
				staleLeave.responseValid = result == ServerProtocol::UserPresence::ELeaveUserResult::StaleOwner;
				staleLeave.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(staleLeave, failure);
			},
			options.userId,
			options.firstLocalClientSessionId,
			firstGeneration);
		if (!ValidateCall("old owner LeaveUser", staleLeaveStart, staleLeave, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState currentRenew;
		const auto currentRenewStart = secondPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FRenewUserRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const ServerProtocol::UserPresence::ERenewUserResult result)
			{
				currentRenew.responseValid = result == ServerProtocol::UserPresence::ERenewUserResult::Renewed;
				currentRenew.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(currentRenew, failure);
			},
			options.userId,
			options.secondLocalClientSessionId,
			secondGeneration);
		if (!ValidateCall("current owner RenewUser", currentRenewStart, currentRenew, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(leaseDurationMilliseconds) + std::chrono::milliseconds(100));
		SCallState expiredOwnerSnapshot;
		const auto expiredOwnerSnapshotStart = secondPeer.GetRpcCommon().Call<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot&)
			{
				expiredOwnerSnapshot.responseValid = result == Cache::Protocol::EPlayerProgressResult::UnauthorizedCaller;
				expiredOwnerSnapshot.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(expiredOwnerSnapshot, failure);
			},
			options.userId,
			options.secondLocalClientSessionId,
			secondGeneration);
		if (!ValidateCall("GetPlayerWorldSnapshot after owner lease expiration",
				expiredOwnerSnapshotStart,
				expiredOwnerSnapshot,
				firstPeer,
				secondPeer,
				timeout))
		{
			return 1;
		}

		SCallState expiredOwnerRenew;
		const auto expiredOwnerRenewStart = secondPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FRenewUserRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const ServerProtocol::UserPresence::ERenewUserResult result)
			{
				expiredOwnerRenew.responseValid = result == ServerProtocol::UserPresence::ERenewUserResult::StaleOwner;
				expiredOwnerRenew.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(expiredOwnerRenew, failure);
			},
			options.userId,
			options.secondLocalClientSessionId,
			secondGeneration);
		if (!ValidateCall("expired owner RenewUser", expiredOwnerRenewStart, expiredOwnerRenew, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		std::uint64_t reenteredGeneration = 0;
		SCallState expiredOwnerReenter;
		const auto expiredOwnerReenterStart = secondPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FEnterUserRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const std::uint64_t userId,
				const ServerProtocol::UserPresence::EEnterUserResult result,
				const std::uint64_t ownerGeneration,
				const std::uint32_t responseLeaseDurationMilliseconds)
			{
				reenteredGeneration = ownerGeneration;
				expiredOwnerReenter.responseValid =
					userId == options.userId && result == ServerProtocol::UserPresence::EEnterUserResult::Entered &&
					ownerGeneration > secondGeneration && responseLeaseDurationMilliseconds == leaseDurationMilliseconds;
				if (expiredOwnerReenter.responseValid)
				{
					secondPeer.SetOwnedUser(userId, ownerGeneration);
				}
				expiredOwnerReenter.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(expiredOwnerReenter, failure);
			},
			options.userId,
			options.secondLocalClientSessionId);
		if (!ValidateCall("expired same owner EnterUser", expiredOwnerReenterStart, expiredOwnerReenter, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		SCallState currentLeave;
		const auto currentLeaveStart = secondPeer.GetRpcCommon().Call<ServerProtocol::UserPresence::FLeaveUserRpc>(
			secondPeer.BuildCacheTarget(options.userId),
			timeout,
			[&](const ServerProtocol::UserPresence::ELeaveUserResult result)
			{
				currentLeave.responseValid = result == ServerProtocol::UserPresence::ELeaveUserResult::Left;
				currentLeave.completed = true;
			},
			[&](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				SetCallFailure(currentLeave, failure);
			},
			options.userId,
			options.secondLocalClientSessionId,
			reenteredGeneration);
		if (!ValidateCall("current owner LeaveUser", currentLeaveStart, currentLeave, firstPeer, secondPeer, timeout))
		{
			return 1;
		}

		std::cout << "[PASS] Cache UserPresence userId=" << options.userId << " oldGameInstance=" << firstPeer.GetGameInstanceId()
				  << " newGameInstance=" << secondPeer.GetGameInstanceId() << " oldGeneration=" << firstGeneration
				  << " replacementGeneration=" << secondGeneration << " reenteredGeneration=" << reenteredGeneration
				  << " level=" << allocationSnapshot.progress.level << " progressVersion=" << allocationSnapshot.progress.progressVersion
				  << " statVersion=" << allocationSnapshot.progress.statVersion << " statRevision=" << unequippedSnapshot.statRevision
				  << " revokeCount=" << firstPeer.GetSuccessfulRevokeCount() << '\n';
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[FAIL] UserPresence smoke setup: " << exception.what() << '\n';
		return 1;
	}
}
