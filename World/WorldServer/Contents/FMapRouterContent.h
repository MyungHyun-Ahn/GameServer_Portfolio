#pragma once

namespace WorldServer::Domain
{
	class FPlayerStatCalculator;
}

namespace WorldServer::Contents
{
	class FWorldSessionRegistry;
	class FWorldSession;

	class FMapRouterContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FMapRouterContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::vector<SMapRoute> routes,
			std::shared_ptr<FWorldSessionRegistry> sessionRegistry,
			std::shared_ptr<Connector::ILoginTicketStore> loginTicketStore,
			SWorldAuthConfig authConfig,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			SCachePresenceConfig cachePresenceConfig,
			std::shared_ptr<const Domain::FPlayerStatCalculator> playerStatCalculator);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		std::uint32_t GetTargetFps() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnFrame(int deltaMilliseconds, ContentsRuntime::Bridge::IContentBridge& bridge) override;

		void ProcessCacheRpcResponse(std::uint64_t rpcSessionId,
			const RpcLib::Protocol::FRpcResponse& response,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void ProcessCacheRpcRequest(std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcRequest& request);
		void ProcessCacheRpcNotification(std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcNotification& notification);
		void FailCacheRpcSession(std::uint64_t rpcSessionId);
		void NotifyCacheReady(std::uint64_t rpcSessionId);
		void NotifyClientDisconnected(std::shared_ptr<FWorldSession> session);

		const SMapRoute* FindRouteByMapDataId(WorldCore::FMapDataId mapDataId) const noexcept;
		const SMapRoute* FindRouteByMapInstanceId(WorldCore::FMapInstanceId mapInstanceId) const noexcept;
		std::size_t GetRouteCount() const noexcept;

	private:
		struct SPendingWorldAuth final
		{
			std::uint64_t requestId = 0;
			ContentsRuntime::Session::FRequestProcessingToken requestToken{};
			std::chrono::steady_clock::time_point deadline{};
		};

		struct SPresenceState final
		{
			std::shared_ptr<FWorldSession> session;
			WorldCore::FUserId userId = WorldCore::kInvalidUserId;
			std::uint64_t ownerGeneration = 0;
			std::chrono::milliseconds leaseDuration{0};
			std::chrono::steady_clock::time_point nextActionAt{};
			bool enterInFlight = false;
			bool snapshotInFlight = false;
			bool renewInFlight = false;
			bool leaveInFlight = false;
			bool ownerReady = false;
			bool disconnected = false;
			bool revokeRequested = false;
			std::optional<SPendingWorldAuth> pendingAuth;
		};

		void HandleWorldAuth(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		bool InitializeCachePresence(const std::shared_ptr<FWorldSession>& session,
			WorldCore::FUserId userId,
			std::optional<SPendingWorldAuth> pendingAuth,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void FlushPendingAuthResponses(ContentsRuntime::Bridge::IContentBridge& bridge);
		void CompleteWorldAuth(SPresenceState& state, ContentsRuntime::Bridge::IContentBridge& bridge);
		void FailWorldAuth(SPresenceState& state, EWorldResultCode resultCode, ContentsRuntime::Bridge::IContentBridge& bridge);
		void StartEnterUser(SPresenceState& state);
		void StartPlayerSnapshot(SPresenceState& state);
		void StartRenewUser(SPresenceState& state);
		void StartLeaveUser(SPresenceState& state);
		void QueueDisconnect(std::uint64_t sessionId);
		void ErasePresence(std::uint64_t sessionId);
		RpcLib::Protocol::FRpcTarget BuildCacheTarget(WorldCore::FUserId userId) const noexcept;
		bool TryConvertSnapshot(const Cache::Protocol::FPlayerWorldSnapshot& source,
			WorldCore::SPlayerRuntimeSnapshot& outSnapshot,
			std::string& outError) const;
		void Log(Foundation::ELogLevel level, const std::string& message) const;

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			Foundation::ELogLevel level,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "WorldServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::vector<SMapRoute> m_routes;
		std::shared_ptr<FWorldSessionRegistry> m_sessionRegistry;
		std::shared_ptr<Connector::ILoginTicketStore> m_loginTicketStore;
		SWorldAuthConfig m_authConfig;
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		SCachePresenceConfig m_cachePresenceConfig;
		std::shared_ptr<const Domain::FPlayerStatCalculator> m_playerStatCalculator;
		RpcLib::Dispatch::FRpcMethodDispatcher m_rpcDispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
		std::unordered_map<std::uint64_t, std::uint64_t> m_sessionGenerations;
		std::unordered_map<std::uint64_t, SPresenceState> m_presenceStates;
		std::unordered_map<WorldCore::FUserId, std::uint64_t> m_sessionIdsByUserId;
		std::unordered_set<std::uint64_t> m_pendingDisconnects;
	};
}
