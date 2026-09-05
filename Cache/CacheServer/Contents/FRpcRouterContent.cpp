#include "CacheServerPch.h"

#include "CacheServer/Contents/FRpcRouterContent.h"

#include "CacheServer/Contents/ContentTypes.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"

namespace CacheServer::Contents
{
	namespace
	{
		constexpr std::chrono::seconds kHandshakeTimeout{5};

		bool IsAcceptedPeerServerType(
			const RpcLib::Protocol::ERpcServerType serverType) noexcept
		{
			return serverType == RpcLib::Protocol::ERpcServerType::Auction || serverType == RpcLib::Protocol::ERpcServerType::Login ||
				   serverType == RpcLib::Protocol::ERpcServerType::Game;
		}
	}

	FRpcRouterContent::FRpcRouterContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		const RpcLib::Protocol::FRpcServerInstanceId serverInstanceId,
		const std::uint64_t maxPacketQueueDepth,
		RpcLib::Session::FRpcSessionRegistry& sessionRegistry,
		RpcLib::Transport::FServerRpcTransport& transport,
		std::vector<ContentsRuntime::Core::FContentInstanceId> playerCacheInstanceIds)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_serverInstanceId(serverInstanceId)
		, m_maxPacketQueueDepth(maxPacketQueueDepth)
		, m_sessionRegistry(sessionRegistry)
		, m_transport(transport)
		, m_playerCacheInstanceIds(std::move(playerCacheInstanceIds))
	{
	}

	ContentsRuntime::Core::FContentId FRpcRouterContent::GetContentId() const noexcept
	{
		return kRpcRouterContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FRpcRouterContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	std::uint64_t FRpcRouterContent::GetMaxPacketQueueDepth() const noexcept
	{
		return m_maxPacketQueueDepth;
	}

	void FRpcRouterContent::OnEnter(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
		if (session == nullptr)
		{
			Log(Foundation::ELogLevel::Error, "RPC session missing on router enter. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		m_handshakeDeadlines.insert_or_assign(sessionId, session->GetConnectedAt() + kHandshakeTimeout);
		Log(Foundation::ELogLevel::Info, "RPC session entered router. sessionId={} routeGeneration={}", sessionId, routeGeneration);
	}

	void FRpcRouterContent::OnLeave(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_handshakeDeadlines.erase(sessionId);
		Log(Foundation::ELogLevel::Info, "RPC session left router. sessionId={} routeGeneration={}", sessionId, routeGeneration);
	}

	void FRpcRouterContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::HelloRequest))
		{
			HandleHello(sessionId, payload, bridge);
			return;
		}

		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Request))
		{
			HandleRequest(sessionId, routeGeneration, payload, bridge);
			return;
		}
		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response))
		{
			HandleResponse(sessionId, routeGeneration, payload, bridge);
			return;
		}
		if (opcode == static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Notification))
		{
			HandleNotification(sessionId, routeGeneration, payload, bridge);
			return;
		}

		Log(Foundation::ELogLevel::Warn, "unexpected RPC wire opcode. sessionId={} opcode={}", sessionId, opcode);
		bridge.DisconnectSession(sessionId);
	}

	void FRpcRouterContent::HandleNotification(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		RpcLib::Protocol::FRpcNotification notification;
		if (!RpcLib::Protocol::DeserializeRpcNotification(payload, notification))
		{
			Log(Foundation::ELogLevel::Warn, "RPC notification deserialize failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
		if (session == nullptr || !session->IsReady())
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC notification rejected from non-ready session. sessionId={} serviceId={} methodId={}",
				sessionId,
				notification.serviceId,
				notification.methodId);
			return;
		}

		if (m_playerCacheInstanceIds.empty())
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC notification dropped because no player cache shard is available. sessionId={} serviceId={} methodId={}",
				sessionId,
				notification.serviceId,
				notification.methodId);
			return;
		}

		const std::size_t shardIndex = GetPlayerCacheShardIndex(notification.routingKey, m_playerCacheInstanceIds.size());
		const auto targetInstanceId = m_playerCacheInstanceIds[shardIndex];
		const auto enqueueResult = bridge.EnqueuePacketToInstance(sessionId,
			routeGeneration,
			targetInstanceId,
			static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Notification),
			payload);
		if (enqueueResult != ContentsRuntime::Core::EPacketEnqueueResult::Accepted)
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC notification enqueue failed. sessionId={} serviceId={} methodId={} routingKey={} shardIndex={} result={}",
				sessionId,
				notification.serviceId,
				notification.methodId,
				notification.routingKey,
				shardIndex,
				static_cast<std::uint8_t>(enqueueResult));
		}
	}

	void FRpcRouterContent::HandleResponse(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		RpcLib::Protocol::FRpcResponse response;
		if (!RpcLib::Protocol::DeserializeRpcResponse(payload, response))
		{
			Log(Foundation::ELogLevel::Warn, "RPC response deserialize failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
		if (session == nullptr || !session->IsReady())
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC response rejected from non-ready session. sessionId={} requestId={}",
				sessionId,
				response.requestId);
			return;
		}

		const auto target = std::find(m_playerCacheInstanceIds.begin(), m_playerCacheInstanceIds.end(), response.originContentInstanceId);
		if (target == m_playerCacheInstanceIds.end())
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC response has invalid origin Content Instance. sessionId={} requestId={} originContentInstanceId={}",
				sessionId,
				response.requestId,
				response.originContentInstanceId);
			return;
		}

		const auto enqueueResult = bridge.EnqueuePacketToInstance(
			sessionId, routeGeneration, *target, static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Response), payload);
		if (enqueueResult != ContentsRuntime::Core::EPacketEnqueueResult::Accepted)
		{
			Log(Foundation::ELogLevel::Warn,
				"RPC response enqueue failed. sessionId={} requestId={} originContentInstanceId={} result={}",
				sessionId,
				response.requestId,
				response.originContentInstanceId,
				static_cast<std::uint8_t>(enqueueResult));
		}
	}

	void FRpcRouterContent::OnFrame(
		const int,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_handshakeDeadlines.begin(); it != m_handshakeDeadlines.end();)
		{
			if (it->second > now)
			{
				++it;
				continue;
			}

			const std::uint64_t sessionId = it->first;
			it = m_handshakeDeadlines.erase(it);
			Log(Foundation::ELogLevel::Warn, "RPC handshake timed out. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
		}
	}

	void FRpcRouterContent::HandleHello(
		const std::uint64_t sessionId,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		RpcLib::Protocol::FRpcHelloRequest request;
		RpcLib::Protocol::FRpcHelloResponse response;
		response.serverType = RpcLib::Protocol::ERpcServerType::Cache;
		response.serverInstanceId = m_serverInstanceId;

		if (!RpcLib::Protocol::DeserializeRpcHelloRequest(payload, request))
		{
			response.result = RpcLib::Protocol::ERpcHelloResult::InvalidServer;
		}
		else if (request.protocolVersion != RpcLib::Protocol::kRpcProtocolVersion)
		{
			response.result = RpcLib::Protocol::ERpcHelloResult::ProtocolMismatch;
		}
		else if (!IsAcceptedPeerServerType(request.serverType) || request.serverInstanceId == 0)
		{
			response.result = RpcLib::Protocol::ERpcHelloResult::InvalidServer;
		}
		else if (!m_sessionRegistry.MarkReady(sessionId, request.serverType, request.serverInstanceId, request.protocolVersion))
		{
			response.result = RpcLib::Protocol::ERpcHelloResult::DuplicateServer;
		}

		if (!m_transport.SendHelloResponse(sessionId, response))
		{
			Log(Foundation::ELogLevel::Error, "Hello response send failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		if (response.result == RpcLib::Protocol::ERpcHelloResult::Success)
		{
			m_handshakeDeadlines.erase(sessionId);
			Log(Foundation::ELogLevel::Info,
				"RPC handshake completed. sessionId={} remoteType={} remoteInstanceId={}",
				sessionId,
				static_cast<std::uint16_t>(request.serverType),
				request.serverInstanceId);
			return;
		}

		Log(Foundation::ELogLevel::Warn,
			"RPC handshake rejected. sessionId={} result={}",
			sessionId,
			static_cast<std::uint16_t>(response.result));
		m_handshakeDeadlines.erase(sessionId);
		bridge.DisconnectSession(sessionId);
	}

	void FRpcRouterContent::HandleRequest(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		RpcLib::Protocol::FRpcRequest request;
		if (!RpcLib::Protocol::DeserializeRpcRequest(payload, request))
		{
			Log(Foundation::ELogLevel::Warn, "RPC request deserialize failed. sessionId={}", sessionId);
			bridge.DisconnectSession(sessionId);
			return;
		}

		const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
		if (session == nullptr || !session->IsReady())
		{
			SendRequestError(sessionId, request, RpcLib::Protocol::ERpcResponseCode::SessionNotReady);
			return;
		}

		if (m_playerCacheInstanceIds.empty())
		{
			SendRequestError(sessionId, request, RpcLib::Protocol::ERpcResponseCode::ServerBusy);
			return;
		}

		const std::size_t shardIndex = GetPlayerCacheShardIndex(request.routingKey, m_playerCacheInstanceIds.size());
		const auto targetInstanceId = m_playerCacheInstanceIds[shardIndex];
		const auto enqueueResult = bridge.EnqueuePacketToInstance(
			sessionId, routeGeneration, targetInstanceId, static_cast<std::uint16_t>(RpcLib::Protocol::ERpcWireOpcode::Request), payload);
		if (enqueueResult != ContentsRuntime::Core::EPacketEnqueueResult::Accepted)
		{
			SendRequestError(sessionId, request, RpcLib::Protocol::ERpcResponseCode::ServerBusy);
			Log(Foundation::ELogLevel::Warn,
				"RPC shard enqueue failed. sessionId={} requestId={} routingKey={} shardIndex={} result={}",
				sessionId,
				request.requestId,
				request.routingKey,
				shardIndex,
				static_cast<std::uint8_t>(enqueueResult));
		}
	}

	void FRpcRouterContent::SendRequestError(
		const std::uint64_t sessionId,
		const RpcLib::Protocol::FRpcRequest& request,
		const RpcLib::Protocol::ERpcResponseCode responseCode)
	{
		RpcLib::Protocol::FRpcResponse response;
		response.requestId = request.requestId;
		response.serviceId = request.serviceId;
		response.methodId = request.methodId;
		response.originContentInstanceId = request.originContentInstanceId;
		response.resultCode = responseCode;
		if (!m_transport.SendResponse(sessionId, response))
		{
			Log(Foundation::ELogLevel::Error,
				"RPC error response send failed. sessionId={} requestId={} result={}",
				sessionId,
				request.requestId,
				static_cast<std::uint16_t>(responseCode));
		}
	}

	void FRpcRouterContent::Log(
		const Foundation::ELogLevel level,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(level, "CacheServer", message);
		}
	}
}
