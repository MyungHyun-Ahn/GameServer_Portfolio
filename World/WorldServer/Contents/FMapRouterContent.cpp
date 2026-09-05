#include "WorldServerPch.h"

#include "WorldServer/Contents/FMapRouterContent.h"

#include "WorldServer/Contents/Session/FWorldSession.h"
#include "WorldServer/Contents/Session/FWorldSessionRegistry.h"
#include "WorldServer/Domain/FPlayerStatCalculator.h"

namespace WorldServer::Contents
{
	namespace
	{
		RpcLib::Client::FOutboundRpcClient& GetRequiredCacheRpcClient(
			const std::shared_ptr<RpcLib::Client::FOutboundRpcClient>& cacheRpcClient)
		{
			if (cacheRpcClient == nullptr)
			{
				throw std::invalid_argument("World map router cache RPC client is null.");
			}
			return *cacheRpcClient;
		}

		Generated::Map::FMapEnterRp BuildMapEnterFailureResponse(
			const std::uint64_t requestId,
			const EWorldResultCode resultCode)
		{
			Generated::Map::FMapEnterRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = requestId;
			return response;
		}

		Generated::World::FWorldAuthRp BuildWorldAuthResponse(
			const std::uint64_t requestId,
			const EWorldResultCode resultCode,
			const WorldCore::FUserId userId = WorldCore::kInvalidUserId)
		{
			Generated::World::FWorldAuthRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.requestId = requestId;
			response.userId = userId;
			return response;
		}

		Generated::Map::FMoveRp BuildMoveFailureResponse(
			const Generated::Map::FMoveRq& request,
			const EWorldResultCode resultCode)
		{
			Generated::Map::FMoveRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.sequence = request.sequence;
			response.moveState = request.moveState;
			response.acceptedPositionX = request.clientPositionX;
			response.acceptedPositionY = request.clientPositionY;
			response.directionX = request.directionX;
			response.directionY = request.directionY;
			response.isCorrected = false;
			return response;
		}

		Generated::Map::FBasicAttackRp BuildBasicAttackFailureResponse(
			const Generated::Map::FBasicAttackRq& request,
			const EWorldResultCode resultCode)
		{
			Generated::Map::FBasicAttackRp response;
			response.resultCode = static_cast<std::uint16_t>(resultCode);
			response.attackSequence = request.attackSequence;
			return response;
		}
	}

	FMapRouterContent::FMapRouterContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::vector<SMapRoute> routes,
		std::shared_ptr<FWorldSessionRegistry> sessionRegistry,
		std::shared_ptr<Connector::ILoginTicketStore> loginTicketStore,
		const SWorldAuthConfig authConfig,
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
		SCachePresenceConfig cachePresenceConfig,
		std::shared_ptr<const Domain::FPlayerStatCalculator> playerStatCalculator)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_routes(std::move(routes))
		, m_sessionRegistry(std::move(sessionRegistry))
		, m_loginTicketStore(std::move(loginTicketStore))
		, m_authConfig(authConfig)
		, m_cacheRpcClient(std::move(cacheRpcClient))
		, m_cachePresenceConfig(cachePresenceConfig)
		, m_playerStatCalculator(std::move(playerStatCalculator))
		, m_rpcCommon(GetRequiredCacheRpcClient(m_cacheRpcClient).GetSessionRegistry(),
			  m_rpcDispatcher,
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetRequestIdGenerator(),
			  GetRequiredCacheRpcClient(m_cacheRpcClient).GetTransport(),
			  contentInstanceId)
	{
		if (m_playerStatCalculator == nullptr)
		{
			throw std::invalid_argument("World map router player stat calculator is null.");
		}

		std::sort(m_routes.begin(),
			m_routes.end(),
			[](const SMapRoute& lhs, const SMapRoute& rhs)
			{
				return lhs.mapDataId < rhs.mapDataId;
			});

		const bool revokeRegistered = m_rpcCommon.Register<ServerProtocol::UserPresence::FRevokeUserRpc>(
			[this](const RpcLib::Dispatch::FRpcCallContext& context,
				RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRevokeUserRpc>& reply,
				const std::uint64_t userId,
				const std::uint64_t localClientSessionId,
				const std::uint64_t ownerGeneration,
				const ServerProtocol::UserPresence::ERevokeUserReason reason)
			{
				using ServerProtocol::UserPresence::ERevokeUserResult;
				if (context.peerServerType != RpcLib::Protocol::ERpcServerType::Cache ||
					context.peerServerInstanceId != m_cachePresenceConfig.cacheServerInstanceId)
				{
					reply.Send(ERevokeUserResult::UnauthorizedCaller);
					return;
				}
				if (userId == 0 || localClientSessionId == 0 || ownerGeneration == 0 || context.routingKey != userId)
				{
					reply.Send(ERevokeUserResult::InvalidRequest);
					return;
				}

				const auto userSessionIt = m_sessionIdsByUserId.find(userId);
				if (userSessionIt == m_sessionIdsByUserId.end())
				{
					reply.Send(ERevokeUserResult::UserNotFound);
					return;
				}
				const auto stateIt = m_presenceStates.find(localClientSessionId);
				if (stateIt == m_presenceStates.end() || stateIt->second.session == nullptr || stateIt->second.userId != userId ||
					stateIt->second.session->GetSessionId() != localClientSessionId)
				{
					reply.Send(ERevokeUserResult::SessionMismatch);
					return;
				}
				if (stateIt->second.ownerGeneration != ownerGeneration)
				{
					reply.Send(ERevokeUserResult::StaleOwner);
					return;
				}

				stateIt->second.ownerReady = false;
				stateIt->second.revokeRequested = true;
				stateIt->second.session->ClearPlayerReady();
				QueueDisconnect(localClientSessionId);
				reply.Send(ERevokeUserResult::Revoked);
				Log(Foundation::ELogLevel::Warn,
					"Cache revoked World owner. userId={} sessionId={} ownerGeneration={} reason={}",
					userId,
					localClientSessionId,
					ownerGeneration,
					static_cast<std::uint8_t>(reason));
			});
		if (!revokeRegistered)
		{
			throw std::runtime_error("World RevokeUser RPC handler registration failed.");
		}
	}

	ContentsRuntime::Core::FContentId FMapRouterContent::GetContentId() const noexcept
	{
		return kMapRouterContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FMapRouterContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint32_t FMapRouterContent::GetTargetFps() const noexcept
	{
		return 20;
	}

	void FMapRouterContent::OnEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr || !session->IsConnected() || !bridge.IsSessionAlive(sessionId))
		{
			return;
		}

		m_sessionGenerations[sessionId] = routeGeneration;
		if (!m_authConfig.enabled && !m_cachePresenceConfig.enabled)
		{
			WorldCore::SPlayerRuntimeSnapshot developmentSnapshot;
			std::string snapshotError;
			if (!m_playerStatCalculator->BuildDevelopmentSnapshot(sessionId, developmentSnapshot, snapshotError) ||
				!session->SetLegacyPlayerReady(sessionId, developmentSnapshot))
			{
				Log(Foundation::ELogLevel::Error,
					"development Player initialization failed. sessionId={} error={}",
					sessionId,
					snapshotError);
				bridge.DisconnectSession(sessionId);
				return;
			}
		}
		else if (!m_authConfig.enabled)
		{
			if (!session->TryBindAuthenticatedUser(sessionId, 0) || !InitializeCachePresence(session, sessionId, std::nullopt, bridge))
			{
				Log(Foundation::ELogLevel::Error, "legacy Cache presence initialization failed. sessionId={}", sessionId);
				bridge.DisconnectSession(sessionId);
				return;
			}
		}
		else
		{
			session->ClearPlayerReady();
		}
		Log(Foundation::ELogLevel::Info,
			"session entered map router. sessionId={} routeGeneration={} routeCount={}",
			sessionId,
			routeGeneration,
			m_routes.size());
	}

	void FMapRouterContent::OnLeave(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto found = m_sessionGenerations.find(sessionId);
		const bool isCurrentGeneration = found != m_sessionGenerations.end() && found->second == routeGeneration;
		if (isCurrentGeneration)
		{
			m_sessionGenerations.erase(found);
		}

		Log(Foundation::ELogLevel::Info,
			"session left map router. sessionId={} routeGeneration={} stale={}",
			sessionId,
			routeGeneration,
			(isCurrentGeneration ? 0 : 1));

		if (!bridge.IsSessionAlive(sessionId) && m_sessionRegistry != nullptr)
		{
			m_sessionRegistry->Remove(sessionId);
		}
	}

	void FMapRouterContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
		const auto generationIt = m_sessionGenerations.find(sessionId);
		if (!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId ||
			generationIt == m_sessionGenerations.end() || generationIt->second != routeGeneration)
		{
			return;
		}

		if (opcode == Generated::World::FWorldAuthRq::kOpcode)
		{
			HandleWorldAuth(sessionId, payload, bridge);
			return;
		}

		if (opcode == Generated::Map::FMoveRq::kOpcode && m_authConfig.enabled)
		{
			Generated::Map::FMoveRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				bridge.DisconnectSession(sessionId);
				return;
			}
			const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
			EWorldResultCode resultCode = EWorldResultCode::MoveRejected;
			if (session == nullptr || !session->IsAuthenticated())
			{
				resultCode = EWorldResultCode::AuthRequired;
			}
			else if (!session->IsPlayerReady())
			{
				resultCode = EWorldResultCode::PlayerNotReady;
			}
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, BuildMoveFailureResponse(request, resultCode));
			return;
		}

		if (opcode == Generated::Map::FBasicAttackRq::kOpcode && m_authConfig.enabled)
		{
			Generated::Map::FBasicAttackRq request;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
			{
				bridge.DisconnectSession(sessionId);
				return;
			}
			const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
			EWorldResultCode resultCode = EWorldResultCode::AttackRejected;
			if (session == nullptr || !session->IsAuthenticated())
			{
				resultCode = EWorldResultCode::AuthRequired;
			}
			else if (!session->IsPlayerReady())
			{
				resultCode = EWorldResultCode::PlayerNotReady;
			}
			(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, BuildBasicAttackFailureResponse(request, resultCode));
			return;
		}

		if (opcode == Generated::World::FEquipItemRq::kOpcode || opcode == Generated::World::FUnequipItemRq::kOpcode)
		{
			std::uint64_t requestId = 0;
			std::uint64_t itemInstanceId = 0;
			std::uint64_t expectedItemVersion = 0;
			if (opcode == Generated::World::FEquipItemRq::kOpcode)
			{
				Generated::World::FEquipItemRq request;
				if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				{
					bridge.DisconnectSession(sessionId);
					return;
				}
				requestId = request.requestId;
				itemInstanceId = request.itemInstanceId;
				expectedItemVersion = request.expectedItemVersion;
			}
			else
			{
				Generated::World::FUnequipItemRq request;
				if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
				{
					bridge.DisconnectSession(sessionId);
					return;
				}
				requestId = request.requestId;
				itemInstanceId = request.itemInstanceId;
				expectedItemVersion = request.expectedItemVersion;
			}

			const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
			const EWorldResultCode resultCode =
				session == nullptr || !session->IsAuthenticated() ? EWorldResultCode::AuthRequired : EWorldResultCode::PlayerNotReady;
			if (opcode == Generated::World::FEquipItemRq::kOpcode)
			{
				Generated::World::FEquipItemRp response;
				response.resultCode = static_cast<std::uint16_t>(resultCode);
				response.requestId = requestId;
				response.itemInstanceId = itemInstanceId;
				response.itemVersion = expectedItemVersion;
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			else
			{
				Generated::World::FUnequipItemRp response;
				response.resultCode = static_cast<std::uint16_t>(resultCode);
				response.requestId = requestId;
				response.itemInstanceId = itemInstanceId;
				response.itemVersion = expectedItemVersion;
				response.equipped = true;
				(void)ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, response);
			}
			return;
		}

		if (opcode != Generated::Map::FMapEnterRq::kOpcode)
		{
			Log(Foundation::ELogLevel::Warn, "map router rejected an unsupported packet. sessionId={} opcode={}", sessionId, opcode);
			return;
		}

		Generated::Map::FMapEnterRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "MapEnter deserialize failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr || !session->IsConnected())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::SessionUnavailable));
			return;
		}
		if (m_authConfig.enabled && !session->IsAuthenticated())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::AuthRequired));
			return;
		}
		if (!session->IsPlayerReady())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::PlayerNotReady));
			return;
		}

		if (request.requestId == 0 || request.mapDataId == WorldCore::kInvalidMapDataId)
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::InvalidRequest));
			return;
		}

		ContentsRuntime::Session::FRequestProcessingToken requestToken{};
		const ContentsRuntime::Session::EBeginRequestResult beginResult = session->TryBeginRequest(
			request.requestId, opcode, ContentsRuntime::Session::ERequestProcessingPolicy::Exclusive, requestToken);
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::Started)
		{
			const EWorldResultCode resultCode = beginResult == ContentsRuntime::Session::EBeginRequestResult::AlreadyProcessing
													? EWorldResultCode::RequestAlreadyProcessing
													: EWorldResultCode::SessionUnavailable;
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, BuildMapEnterFailureResponse(request.requestId, resultCode));
			return;
		}

		const SMapRoute* const route = FindRouteByMapDataId(request.mapDataId);
		if (route == nullptr || !bridge.HasContentInstance(route->contentInstanceId))
		{
			Generated::Map::FMapEnterRp response = BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::MapNotFound);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			return;
		}

		if (!session->SetPendingMapEnter(request.requestId, request.mapDataId, requestToken))
		{
			Generated::Map::FMapEnterRp response = BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::InternalError);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response);
			return;
		}

		std::vector<char> ownedPayload(payload.begin(), payload.end());
		const ContentsRuntime::Core::FContentInstanceId targetContentInstanceId = route->contentInstanceId;
		const std::uint64_t targetRouteGeneration = routeGeneration + 1;
		ContentsRuntime::Bridge::IContentBridge* const bridgePointer = &bridge;
		const bool moveAccepted = bridge.MoveSessionToInstanceWithCompletion(sessionId,
			targetContentInstanceId,
			[bridgePointer,
				session,
				sessionId,
				requestId = request.requestId,
				mapDataId = request.mapDataId,
				targetContentInstanceId,
				targetRouteGeneration,
				ownedPayload = std::move(ownedPayload)]() mutable
			{
				const auto enqueueResult = bridgePointer->EnqueuePacketToInstance(sessionId,
					targetRouteGeneration,
					targetContentInstanceId,
					Generated::Map::FMapEnterRq::kOpcode,
					std::span<const char>(ownedPayload.data(), ownedPayload.size()));
				if (enqueueResult == ContentsRuntime::Core::EPacketEnqueueResult::Accepted)
				{
					return;
				}

				const std::optional<SPendingMapEnter> pending = session->ConsumePendingMapEnter(requestId, mapDataId);
				if (pending.has_value())
				{
					const EWorldResultCode resultCode = enqueueResult == ContentsRuntime::Core::EPacketEnqueueResult::QueueFull
															? EWorldResultCode::ServerBusy
															: EWorldResultCode::RouteFailure;
					Generated::Map::FMapEnterRp response = BuildMapEnterFailureResponse(requestId, resultCode);
					ContentsRuntime::Bridge::SendContentPacket(*bridgePointer, *session, pending->requestToken, response);
				}

				if (bridgePointer->IsSessionAlive(sessionId))
				{
					bridgePointer->DisconnectSession(sessionId);
				}
			});

		if (moveAccepted)
		{
			return;
		}

		const std::optional<SPendingMapEnter> pending = session->ConsumePendingMapEnter(request.requestId, request.mapDataId);
		if (pending.has_value())
		{
			Generated::Map::FMapEnterRp response = BuildMapEnterFailureResponse(request.requestId, EWorldResultCode::RouteFailure);
			ContentsRuntime::Bridge::SendContentPacket(bridge, *session, pending->requestToken, response);
		}
		else
		{
			session->CancelRequest(requestToken);
		}

		Log(Foundation::ELogLevel::Error,
			"map route transition failed; session will be disconnected. sessionId={} requestId={} mapDataId={}",
			sessionId,
			request.requestId,
			request.mapDataId);
		if (bridge.IsSessionAlive(sessionId))
		{
			bridge.DisconnectSession(sessionId);
		}
	}

	void FMapRouterContent::HandleWorldAuth(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		Generated::World::FWorldAuthRq request;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(Generated::World::FWorldAuthRq::kOpcode, payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "WorldAuth deserialize failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		const std::shared_ptr<FWorldSession> session = m_sessionRegistry != nullptr ? m_sessionRegistry->Find(sessionId) : nullptr;
		if (session == nullptr || !session->IsConnected())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildWorldAuthResponse(request.requestId, EWorldResultCode::SessionUnavailable));
			return;
		}
		if (!m_authConfig.enabled || session->IsAuthenticated())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildWorldAuthResponse(request.requestId, EWorldResultCode::AlreadyAuthenticated));
			return;
		}
		if (request.requestId == 0 || request.ticket.empty())
		{
			ContentsRuntime::Bridge::SendContentPacket(
				bridge, sessionId, BuildWorldAuthResponse(request.requestId, EWorldResultCode::InvalidRequest));
			return;
		}

		ContentsRuntime::Session::FRequestProcessingToken requestToken{};
		const ContentsRuntime::Session::EBeginRequestResult beginResult = session->TryBeginRequest(request.requestId,
			Generated::World::FWorldAuthRq::kOpcode,
			ContentsRuntime::Session::ERequestProcessingPolicy::Exclusive,
			requestToken);
		if (beginResult != ContentsRuntime::Session::EBeginRequestResult::Started)
		{
			const EWorldResultCode resultCode = beginResult == ContentsRuntime::Session::EBeginRequestResult::AlreadyProcessing
													? EWorldResultCode::RequestAlreadyProcessing
													: EWorldResultCode::SessionUnavailable;
			ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, BuildWorldAuthResponse(request.requestId, resultCode));
			return;
		}

		auto sendFailure = [&](const EWorldResultCode resultCode)
		{
			const Generated::World::FWorldAuthRp response = BuildWorldAuthResponse(request.requestId, resultCode);
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response))
			{
				session->CancelRequest(requestToken);
			}
		};

		Connector::SConsumedLoginTicket consumedTicket{};
		std::string authError;
		if (m_loginTicketStore == nullptr || !m_loginTicketStore->TryConsumeLoginTicket(request.ticket, consumedTicket, authError) ||
			!consumedTicket.valid || consumedTicket.userId == 0)
		{
			sendFailure(EWorldResultCode::AuthenticationFailed);
			Log(Foundation::ELogLevel::Warn, "WorldAuth rejected. sessionId={} reason={}", sessionId, authError);
			return;
		}
		if (consumedTicket.targetServerInstanceId == 0 || consumedTicket.targetServerInstanceId != m_authConfig.worldServerInstanceId)
		{
			sendFailure(EWorldResultCode::AuthenticationFailed);
			Log(Foundation::ELogLevel::Warn,
				"WorldAuth target rejected. sessionId={} ticketTarget={} localInstance={}",
				sessionId,
				consumedTicket.targetServerInstanceId,
				m_authConfig.worldServerInstanceId);
			return;
		}
		if (!session->TryBindAuthenticatedUser(consumedTicket.userId, consumedTicket.loginVersion))
		{
			sendFailure(EWorldResultCode::AlreadyAuthenticated);
			return;
		}

		if (!m_cachePresenceConfig.enabled)
		{
			WorldCore::SPlayerRuntimeSnapshot developmentSnapshot;
			std::string snapshotError;
			if (!m_playerStatCalculator->BuildDevelopmentSnapshot(consumedTicket.userId, developmentSnapshot, snapshotError) ||
				!session->SetAuthenticatedPlayerReady(developmentSnapshot))
			{
				Log(Foundation::ELogLevel::Error,
					"authenticated development Player initialization failed. sessionId={} userId={} error={}",
					sessionId,
					consumedTicket.userId,
					snapshotError);
				sendFailure(EWorldResultCode::InternalError);
				bridge.DisconnectSession(sessionId);
				return;
			}
			const Generated::World::FWorldAuthRp response =
				BuildWorldAuthResponse(request.requestId, EWorldResultCode::Success, consumedTicket.userId);
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *session, requestToken, response))
			{
				session->CancelRequest(requestToken);
			}
			Log(Foundation::ELogLevel::Info,
				"WorldAuth succeeded without Cache. sessionId={} userId={} loginVersion={}",
				sessionId,
				consumedTicket.userId,
				consumedTicket.loginVersion);
			return;
		}

		SPendingWorldAuth pendingAuth;
		pendingAuth.requestId = request.requestId;
		pendingAuth.requestToken = requestToken;
		pendingAuth.deadline = std::chrono::steady_clock::now() + m_cachePresenceConfig.rpcTimeout;
		if (!InitializeCachePresence(session, consumedTicket.userId, std::move(pendingAuth), bridge))
		{
			sendFailure(EWorldResultCode::InternalError);
			bridge.DisconnectSession(sessionId);
			return;
		}
		Log(Foundation::ELogLevel::Info,
			"WorldAuth ticket accepted; waiting for Cache Snapshot. sessionId={} userId={} loginVersion={}",
			sessionId,
			consumedTicket.userId,
			consumedTicket.loginVersion);
	}

	bool FMapRouterContent::InitializeCachePresence(
		const std::shared_ptr<FWorldSession>& session,
		const WorldCore::FUserId userId,
		std::optional<SPendingWorldAuth> pendingAuth,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (!m_cachePresenceConfig.enabled || session == nullptr || userId == WorldCore::kInvalidUserId ||
			m_presenceStates.contains(session->GetSessionId()))
		{
			return false;
		}

		const std::uint64_t sessionId = session->GetSessionId();
		const auto previousSessionIt = m_sessionIdsByUserId.find(userId);
		if (previousSessionIt != m_sessionIdsByUserId.end() && previousSessionIt->second != sessionId)
		{
			const std::uint64_t previousSessionId = previousSessionIt->second;
			const auto previousStateIt = m_presenceStates.find(previousSessionId);
			if (previousStateIt != m_presenceStates.end())
			{
				previousStateIt->second.revokeRequested = true;
				if (previousStateIt->second.session != nullptr)
				{
					previousStateIt->second.session->ClearPlayerReady();
				}
			}
			QueueDisconnect(previousSessionId);
			if (bridge.IsSessionAlive(previousSessionId))
			{
				bridge.DisconnectSession(previousSessionId);
			}
			Log(Foundation::ELogLevel::Info,
				"WorldAuth replaced a local session. userId={} previousSessionId={} replacementSessionId={}",
				userId,
				previousSessionId,
				sessionId);
		}

		SPresenceState state;
		state.session = session;
		state.userId = userId;
		state.nextActionAt = std::chrono::steady_clock::now();
		state.pendingAuth = std::move(pendingAuth);
		m_presenceStates.emplace(sessionId, std::move(state));
		m_sessionIdsByUserId[userId] = sessionId;
		session->ClearPlayerReady();
		return true;
	}

	void FMapRouterContent::CompleteWorldAuth(
		SPresenceState& state,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (!state.pendingAuth.has_value() || state.session == nullptr)
		{
			return;
		}

		const SPendingWorldAuth pendingAuth = *state.pendingAuth;
		state.pendingAuth.reset();
		const Generated::World::FWorldAuthRp response =
			BuildWorldAuthResponse(pendingAuth.requestId, EWorldResultCode::Success, state.userId);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *state.session, pendingAuth.requestToken, response))
		{
			state.session->CancelRequest(pendingAuth.requestToken);
			QueueDisconnect(state.session->GetSessionId());
			return;
		}

		Log(Foundation::ELogLevel::Info,
			"WorldAuth succeeded after Cache Snapshot. sessionId={} userId={} loginVersion={}",
			state.session->GetSessionId(),
			state.userId,
			state.session->GetLoginVersion());
	}

	void FMapRouterContent::FailWorldAuth(
		SPresenceState& state,
		const EWorldResultCode resultCode,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (!state.pendingAuth.has_value() || state.session == nullptr)
		{
			return;
		}

		const SPendingWorldAuth pendingAuth = *state.pendingAuth;
		state.pendingAuth.reset();
		const Generated::World::FWorldAuthRp response = BuildWorldAuthResponse(pendingAuth.requestId, resultCode);
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, *state.session, pendingAuth.requestToken, response))
		{
			state.session->CancelRequest(pendingAuth.requestToken);
		}
	}

	void FMapRouterContent::FlushPendingAuthResponses(
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto& [sessionId, state] : m_presenceStates)
		{
			if (!state.pendingAuth.has_value())
			{
				continue;
			}
			if (state.pendingAuth->deadline != std::chrono::steady_clock::time_point{} && now >= state.pendingAuth->deadline)
			{
				FailWorldAuth(state, EWorldResultCode::CacheUnavailable, bridge);
				QueueDisconnect(sessionId);
			}
			else if (state.ownerReady && state.session != nullptr && state.session->IsPlayerReady())
			{
				CompleteWorldAuth(state, bridge);
			}
			else if (state.disconnected || state.revokeRequested || m_pendingDisconnects.contains(sessionId) || state.session == nullptr ||
					 !state.session->IsConnected())
			{
				FailWorldAuth(state, EWorldResultCode::CacheUnavailable, bridge);
			}
		}
	}

	void FMapRouterContent::OnFrame(
		const int,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (!m_cachePresenceConfig.enabled)
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		m_rpcCommon.ProcessTimeouts(now);
		FlushPendingAuthResponses(bridge);

		std::vector<std::uint64_t> eraseSessionIds;
		for (auto& [sessionId, state] : m_presenceStates)
		{
			if (state.session == nullptr)
			{
				eraseSessionIds.push_back(sessionId);
				continue;
			}
			if (m_pendingDisconnects.contains(sessionId))
			{
				state.disconnected = true;
			}
			if (state.disconnected || state.revokeRequested || !state.session->IsConnected())
			{
				state.disconnected = true;
				if (state.ownerGeneration != 0 && !state.leaveInFlight && m_cacheRpcClient->IsReady())
				{
					StartLeaveUser(state);
				}
				else if ((state.ownerGeneration != 0 && !m_cacheRpcClient->IsReady()) ||
						 (state.ownerGeneration == 0 && !state.enterInFlight && !state.snapshotInFlight && !state.renewInFlight &&
							 !state.leaveInFlight))
				{
					eraseSessionIds.push_back(sessionId);
				}
				continue;
			}
			if (!m_cacheRpcClient->IsReady() || now < state.nextActionAt)
			{
				continue;
			}
			if (state.ownerGeneration == 0 && !state.enterInFlight)
			{
				StartEnterUser(state);
			}
			else if (!state.ownerReady && !state.snapshotInFlight)
			{
				StartPlayerSnapshot(state);
			}
			else if (state.ownerReady && !state.renewInFlight)
			{
				StartRenewUser(state);
			}
		}

		for (const std::uint64_t sessionId : m_pendingDisconnects)
		{
			const auto stateIt = m_presenceStates.find(sessionId);
			if (stateIt != m_presenceStates.end())
			{
				stateIt->second.disconnected = true;
			}
			if (bridge.IsSessionAlive(sessionId))
			{
				bridge.DisconnectSession(sessionId);
			}
		}
		m_pendingDisconnects.clear();

		for (const std::uint64_t sessionId : eraseSessionIds)
		{
			ErasePresence(sessionId);
		}
	}

	void FMapRouterContent::ProcessCacheRpcResponse(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcResponse& response,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto result = m_rpcCommon.ProcessResponse(rpcSessionId, response);
		if (result != RpcLib::Protocol::ERpcCompletionResult::Completed)
		{
			Log(Foundation::ELogLevel::Warn,
				"World Cache RPC response was not completed. requestId={} result={}",
				response.requestId,
				static_cast<std::uint8_t>(result));
		}
		FlushPendingAuthResponses(bridge);
	}

	void FMapRouterContent::ProcessCacheRpcRequest(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcRequest& request)
	{
		RpcLib::Protocol::FRpcResponse response = m_rpcCommon.DispatchRequest(rpcSessionId, request);
		if (!m_cacheRpcClient->GetTransport().SendResponse(rpcSessionId, response))
		{
			Log(Foundation::ELogLevel::Error,
				"World Cache RPC response send failed. requestId={} serviceId={} methodId={}",
				request.requestId,
				request.serviceId,
				request.methodId);
		}
	}

	void FMapRouterContent::ProcessCacheRpcNotification(
		const std::uint64_t rpcSessionId,
		const RpcLib::Protocol::FRpcNotification& notification)
	{
		const auto result = m_rpcCommon.DispatchNotification(rpcSessionId, notification);
		if (result != RpcLib::Protocol::ERpcNotificationDispatchResult::Dispatched)
		{
			Log(Foundation::ELogLevel::Warn,
				"World Cache RPC notification was not dispatched. serviceId={} methodId={} result={}",
				notification.serviceId,
				notification.methodId,
				static_cast<std::uint8_t>(result));
		}
	}

	void FMapRouterContent::FailCacheRpcSession(
		const std::uint64_t rpcSessionId)
	{
		m_rpcCommon.FailSession(rpcSessionId, RpcLib::Protocol::ERpcCallError::Disconnected);
		Log(Foundation::ELogLevel::Warn,
			"World Cache RPC disconnected; revoking local player readiness. rpcSessionId={} playerSessionCount={}",
			rpcSessionId,
			m_presenceStates.size());
		for (auto& [sessionId, state] : m_presenceStates)
		{
			state.ownerReady = false;
			state.session->ClearPlayerReady();
			QueueDisconnect(sessionId);
		}
	}

	void FMapRouterContent::NotifyCacheReady(
		const std::uint64_t rpcSessionId)
	{
		Log(Foundation::ELogLevel::Info, "World Cache RPC ready. rpcSessionId={}", rpcSessionId);
		const auto now = std::chrono::steady_clock::now();
		for (auto& [sessionId, state] : m_presenceStates)
		{
			(void)sessionId;
			state.nextActionAt = now;
		}
	}

	void FMapRouterContent::NotifyClientDisconnected(
		std::shared_ptr<FWorldSession> session)
	{
		if (session == nullptr)
		{
			return;
		}
		const auto stateIt = m_presenceStates.find(session->GetSessionId());
		if (stateIt != m_presenceStates.end())
		{
			stateIt->second.disconnected = true;
			stateIt->second.session = std::move(session);
		}
	}

	void FMapRouterContent::StartEnterUser(
		SPresenceState& state)
	{
		state.enterInFlight = true;
		const std::uint64_t sessionId = state.session->GetSessionId();
		const auto callResult = m_rpcCommon.Call<ServerProtocol::UserPresence::FEnterUserRpc>(
			BuildCacheTarget(state.userId),
			m_cachePresenceConfig.rpcTimeout,
			[this, sessionId](const std::uint64_t userId,
				const ServerProtocol::UserPresence::EEnterUserResult result,
				const std::uint64_t ownerGeneration,
				const std::uint32_t leaseDurationMilliseconds)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt == m_presenceStates.end())
				{
					return;
				}
				SPresenceState& current = stateIt->second;
				current.enterInFlight = false;
				const bool accepted = result == ServerProtocol::UserPresence::EEnterUserResult::Entered ||
									  result == ServerProtocol::UserPresence::EEnterUserResult::AlreadyEntered ||
									  result == ServerProtocol::UserPresence::EEnterUserResult::ReplacedPreviousGameServer;
				if (!accepted || userId != current.userId || ownerGeneration == 0 || leaseDurationMilliseconds == 0)
				{
					Log(Foundation::ELogLevel::Error,
						"World EnterUser rejected. userId={} sessionId={} result={}",
						current.userId,
						sessionId,
						static_cast<std::uint8_t>(result));
					QueueDisconnect(sessionId);
					return;
				}
				current.ownerGeneration = ownerGeneration;
				current.leaseDuration = std::chrono::milliseconds(leaseDurationMilliseconds);
				if (current.disconnected || current.revokeRequested || current.session == nullptr || !current.session->IsConnected())
				{
					QueueDisconnect(sessionId);
					return;
				}
				current.nextActionAt = std::chrono::steady_clock::now();
				StartPlayerSnapshot(current);
			},
			[this, sessionId](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt == m_presenceStates.end())
				{
					return;
				}
				stateIt->second.enterInFlight = false;
				if (stateIt->second.pendingAuth.has_value())
				{
					QueueDisconnect(sessionId);
				}
				else
				{
					stateIt->second.nextActionAt = std::chrono::steady_clock::now() + m_cachePresenceConfig.retryInterval;
				}
				Log(Foundation::ELogLevel::Warn,
					"World EnterUser RPC failed. sessionId={} error={}",
					sessionId,
					static_cast<std::uint8_t>(failure.error));
			},
			state.userId,
			sessionId);
		if (!callResult.accepted)
		{
			state.enterInFlight = false;
			if (state.pendingAuth.has_value())
			{
				QueueDisconnect(sessionId);
			}
			else
			{
				state.nextActionAt = std::chrono::steady_clock::now() + m_cachePresenceConfig.retryInterval;
			}
		}
	}

	void FMapRouterContent::StartPlayerSnapshot(
		SPresenceState& state)
	{
		if (state.ownerGeneration == 0 || state.snapshotInFlight)
		{
			return;
		}
		state.snapshotInFlight = true;
		const std::uint64_t sessionId = state.session->GetSessionId();
		const std::uint64_t ownerGeneration = state.ownerGeneration;
		const auto callResult = m_rpcCommon.Call<Cache::Protocol::FGetPlayerWorldSnapshotRpc>(
			BuildCacheTarget(state.userId),
			m_cachePresenceConfig.rpcTimeout,
			[this, sessionId, ownerGeneration](
				const Cache::Protocol::EPlayerProgressResult result, const Cache::Protocol::FPlayerWorldSnapshot& snapshot)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt == m_presenceStates.end() || stateIt->second.ownerGeneration != ownerGeneration)
				{
					return;
				}
				SPresenceState& current = stateIt->second;
				current.snapshotInFlight = false;
				if (current.disconnected || current.revokeRequested || current.session == nullptr || !current.session->IsConnected())
				{
					QueueDisconnect(sessionId);
					return;
				}
				WorldCore::SPlayerRuntimeSnapshot runtimeSnapshot;
				std::string snapshotError;
				if (result != Cache::Protocol::EPlayerProgressResult::Success ||
					!TryConvertSnapshot(snapshot, runtimeSnapshot, snapshotError) ||
					!current.session->SetCachePlayerReady(current.userId, ownerGeneration, runtimeSnapshot))
				{
					Log(Foundation::ELogLevel::Error,
						"World Snapshot initialization failed. userId={} sessionId={} result={} error={}",
						current.userId,
						sessionId,
						static_cast<std::uint8_t>(result),
						snapshotError);
					QueueDisconnect(sessionId);
					return;
				}
				current.ownerReady = true;
				current.nextActionAt = std::chrono::steady_clock::now() + current.leaseDuration / 2;
				Log(Foundation::ELogLevel::Info,
					"World player initialized from Cache Snapshot. userId={} sessionId={} characterId={} level={} hp={}/{} mp={}/{} "
					"attack={} defense={} statRevision={}",
					current.userId,
					sessionId,
					runtimeSnapshot.characterId,
					runtimeSnapshot.level,
					runtimeSnapshot.maxHp,
					runtimeSnapshot.maxHp,
					runtimeSnapshot.maxMp,
					runtimeSnapshot.maxMp,
					runtimeSnapshot.attack,
					runtimeSnapshot.defense,
					runtimeSnapshot.statRevision);
			},
			[this, sessionId](const RpcLib::Protocol::FRpcCallFailure& failure)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt == m_presenceStates.end())
				{
					return;
				}
				stateIt->second.snapshotInFlight = false;
				Log(Foundation::ELogLevel::Error,
					"World Snapshot RPC failed. sessionId={} error={}",
					sessionId,
					static_cast<std::uint8_t>(failure.error));
				QueueDisconnect(sessionId);
			},
			state.userId,
			sessionId,
			ownerGeneration);
		if (!callResult.accepted)
		{
			state.snapshotInFlight = false;
			if (state.pendingAuth.has_value())
			{
				QueueDisconnect(sessionId);
			}
			else
			{
				state.nextActionAt = std::chrono::steady_clock::now() + m_cachePresenceConfig.retryInterval;
			}
		}
	}

	void FMapRouterContent::StartRenewUser(
		SPresenceState& state)
	{
		state.renewInFlight = true;
		const std::uint64_t sessionId = state.session->GetSessionId();
		const std::uint64_t ownerGeneration = state.ownerGeneration;
		const auto callResult = m_rpcCommon.Call<ServerProtocol::UserPresence::FRenewUserRpc>(
			BuildCacheTarget(state.userId),
			m_cachePresenceConfig.rpcTimeout,
			[this, sessionId, ownerGeneration](const ServerProtocol::UserPresence::ERenewUserResult result)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt == m_presenceStates.end() || stateIt->second.ownerGeneration != ownerGeneration)
				{
					return;
				}
				stateIt->second.renewInFlight = false;
				if (stateIt->second.disconnected || stateIt->second.revokeRequested || stateIt->second.session == nullptr ||
					!stateIt->second.session->IsConnected())
				{
					QueueDisconnect(sessionId);
					return;
				}
				if (result != ServerProtocol::UserPresence::ERenewUserResult::Renewed)
				{
					QueueDisconnect(sessionId);
					return;
				}
				stateIt->second.nextActionAt = std::chrono::steady_clock::now() + stateIt->second.leaseDuration / 2;
			},
			[this, sessionId](const RpcLib::Protocol::FRpcCallFailure&)
			{
				const auto stateIt = m_presenceStates.find(sessionId);
				if (stateIt != m_presenceStates.end())
				{
					stateIt->second.renewInFlight = false;
				}
				QueueDisconnect(sessionId);
			},
			state.userId,
			sessionId,
			ownerGeneration);
		if (!callResult.accepted)
		{
			state.renewInFlight = false;
			QueueDisconnect(sessionId);
		}
	}

	void FMapRouterContent::StartLeaveUser(
		SPresenceState& state)
	{
		state.leaveInFlight = true;
		const std::uint64_t sessionId = state.session->GetSessionId();
		const std::uint64_t ownerGeneration = state.ownerGeneration;
		const auto callResult = m_rpcCommon.Call<ServerProtocol::UserPresence::FLeaveUserRpc>(
			BuildCacheTarget(state.userId),
			m_cachePresenceConfig.rpcTimeout,
			[this, sessionId](const ServerProtocol::UserPresence::ELeaveUserResult)
			{
				ErasePresence(sessionId);
			},
			[this, sessionId](const RpcLib::Protocol::FRpcCallFailure&)
			{
				ErasePresence(sessionId);
			},
			state.userId,
			sessionId,
			ownerGeneration);
		if (!callResult.accepted)
		{
			state.leaveInFlight = false;
			state.ownerGeneration = 0;
		}
	}

	void FMapRouterContent::QueueDisconnect(
		const std::uint64_t sessionId)
	{
		if (sessionId != 0)
		{
			m_pendingDisconnects.insert(sessionId);
		}
	}

	void FMapRouterContent::ErasePresence(
		const std::uint64_t sessionId)
	{
		const auto stateIt = m_presenceStates.find(sessionId);
		if (stateIt == m_presenceStates.end())
		{
			return;
		}
		const auto userSessionIt = m_sessionIdsByUserId.find(stateIt->second.userId);
		if (userSessionIt != m_sessionIdsByUserId.end() && userSessionIt->second == sessionId)
		{
			m_sessionIdsByUserId.erase(userSessionIt);
		}
		m_presenceStates.erase(stateIt);
	}

	RpcLib::Protocol::FRpcTarget FMapRouterContent::BuildCacheTarget(
		const WorldCore::FUserId userId) const noexcept
	{
		RpcLib::Protocol::FRpcTarget target;
		target.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		target.serverInstanceId = m_cachePresenceConfig.cacheServerInstanceId;
		target.routingKey = userId;
		return target;
	}

	bool FMapRouterContent::TryConvertSnapshot(
		const Cache::Protocol::FPlayerWorldSnapshot& source,
		WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
		std::string& outError) const
	{
		return m_playerStatCalculator != nullptr && m_playerStatCalculator->Calculate(source, outSnapshot, outError);
	}

	const SMapRoute* FMapRouterContent::FindRouteByMapDataId(
		const WorldCore::FMapDataId mapDataId) const noexcept
	{
		const auto found = std::lower_bound(m_routes.begin(),
			m_routes.end(),
			mapDataId,
			[](const SMapRoute& route, const WorldCore::FMapDataId value)
			{
				return route.mapDataId < value;
			});
		return found != m_routes.end() && found->mapDataId == mapDataId ? &*found : nullptr;
	}

	const SMapRoute* FMapRouterContent::FindRouteByMapInstanceId(
		const WorldCore::FMapInstanceId mapInstanceId) const noexcept
	{
		const auto found = std::find_if(m_routes.begin(),
			m_routes.end(),
			[mapInstanceId](const SMapRoute& route)
			{
				return route.mapInstanceId == mapInstanceId;
			});
		return found == m_routes.end() ? nullptr : &*found;
	}

	std::size_t FMapRouterContent::GetRouteCount() const noexcept
	{
		return m_routes.size();
	}

	void FMapRouterContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "WorldServer", message);
		}
	}
}
