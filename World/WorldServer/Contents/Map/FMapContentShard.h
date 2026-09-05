#pragma once

namespace WorldCore
{
	class FMapInstance;
	class FMapInstanceManager;
}

namespace WorldServer::Domain
{
	class FPlayerStatCalculator;
}

namespace WorldServer::Contents
{
	class FWorldSessionRegistry;
	class FWorldSession;
	class FTaskGraphSectorExecutionService;

	class FMapContentShard final : public ContentsRuntime::Core::IContent
	{
	public:
		FMapContentShard(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::uint32_t shardIndex,
			std::uint32_t shardCount,
			std::uint32_t targetFps,
			std::uint64_t maxPacketQueueDepth,
			std::shared_ptr<FWorldSessionRegistry> sessionRegistry,
			std::shared_ptr<FTaskGraphSectorExecutionService> taskGraphExecutionService,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			SCachePresenceConfig cachePresenceConfig,
			std::shared_ptr<const Domain::FPlayerStatCalculator> playerStatCalculator);
		~FMapContentShard() override;

		bool Initialize(const std::vector<SBootMapDefinition>& mapDefinitions, std::string& outError);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		std::uint32_t GetTargetFps() const noexcept override;
		std::uint64_t GetMaxPacketQueueDepth() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnFrame(int delayFrame, ContentsRuntime::Bridge::IContentBridge& bridge) override;

		std::uint32_t GetShardIndex() const noexcept;
		std::size_t GetMapCount() const noexcept;
		void ProcessCacheRpcResponse(std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcResponse& response);
		void FailCacheRpcSession(std::uint64_t rpcSessionId);

	private:
		struct SLocalSessionState final
		{
			std::uint64_t routeGeneration = 0;
			WorldCore::FMapInstanceId mapInstanceId = WorldCore::kInvalidMapInstanceId;
			WorldCore::FEntityId entityId = WorldCore::kInvalidEntityId;
			WorldCore::FUserId userId = WorldCore::kInvalidUserId;
			WorldCore::FMoveSequence inFlightMoveSequence = 0;
			WorldCore::FMoveSequence queuedMoveSequence = 0;
			std::unordered_set<WorldCore::FAttackSequence> queuedAttackSequences;
			std::unordered_set<WorldCore::FAttackSequence> inFlightAttackSequences;
		};

		struct SDeferredMapEnter final
		{
			std::uint64_t sessionId = 0;
			std::uint64_t routeGeneration = 0;
			std::uint64_t requestId = 0;
			WorldCore::FMapDataId mapDataId = WorldCore::kInvalidMapDataId;
			WorldCore::FMapInstanceId mapInstanceId = WorldCore::kInvalidMapInstanceId;
			ContentsRuntime::Session::FRequestProcessingToken requestToken{};
		};

		enum class EEquipmentMutation : std::uint8_t
		{
			Equip = 1,
			Unequip = 2
		};

		struct SPendingEquipmentMutation final
		{
			std::shared_ptr<FWorldSession> session;
			EEquipmentMutation mutation = EEquipmentMutation::Equip;
			std::uint64_t sessionId = 0;
			std::uint64_t routeGeneration = 0;
			std::uint64_t requestId = 0;
			std::uint64_t itemInstanceId = 0;
			std::uint64_t expectedItemVersion = 0;
			std::uint64_t expectedStatRevision = 0;
			std::uint64_t expectedEquipmentVersion = 0;
			WorldCore::FMapInstanceId mapInstanceId = WorldCore::kInvalidMapInstanceId;
			WorldCore::FEntityId entityId = WorldCore::kInvalidEntityId;
			ContentsRuntime::Session::FRequestProcessingToken requestToken{};
			EWorldResultCode resultCode = EWorldResultCode::CacheUnavailable;
			WorldCore::SPlayerRuntimeSnapshot runtimeSnapshot{};
			std::uint64_t resultItemVersion = 0;
			bool resultEquipped = false;
			bool disconnectAfterResponse = false;
			bool rpcCompleted = false;
		};

		void HandleMapEnter(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void CompleteMapEnter(const SDeferredMapEnter& deferredEnter, ContentsRuntime::Bridge::IContentBridge& bridge);
		void CancelDeferredMapEnter(std::uint64_t sessionId);
		void ProcessDeferredExternalEvents(ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleEquipmentMutation(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void ProcessDeferredEquipmentMutations(ContentsRuntime::Bridge::IContentBridge& bridge);
		void CancelPendingEquipmentMutation(std::uint64_t sessionId);
		[[nodiscard]] RpcLib::Protocol::FRpcTarget BuildCacheTarget(WorldCore::FUserId userId) const noexcept;
		[[nodiscard]] static EWorldResultCode MapEquipmentResult(Cache::Protocol::EPlayerEquipmentResult result) noexcept;
		void HandleTaskGraphCompletion(WorldCore::SMapTickExecutionCompletion completion);
		void TrackStartedTickMoves(const WorldCore::SMapTickResult& tickResult);
		void TrackStartedTickAttacks(const WorldCore::SMapTickResult& tickResult);
		void HandleMove(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void HandleBasicAttack(std::uint64_t sessionId, std::span<const char> payload, ContentsRuntime::Bridge::IContentBridge& bridge);
		void DispatchVisibilityEvents(const WorldCore::FMapInstance& mapInstance,
			std::span<const WorldCore::SVisibilityEvent> visibilityEvents,
			std::uint64_t serverTick,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void DispatchActorAttackEvents(std::span<const WorldCore::SActorAttackEvent> attackEvents,
			std::uint64_t serverTick,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void DispatchActorDeathEvents(std::span<const WorldCore::SActorDeathEvent> deathEvents,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void DispatchActorRespawnEvents(std::span<const WorldCore::SActorRespawnEvent> respawnEvents,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void FailTickMoves(WorldCore::FMapInstanceId mapInstanceId,
			std::span<const WorldCore::SMoveRequestIdentity> moveRequests,
			EWorldResultCode resultCode,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void FailQueuedMoves(WorldCore::FMapInstanceId mapInstanceId,
			std::span<const WorldCore::SMoveRequestIdentity> moveRequests,
			EWorldResultCode resultCode,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void FailTickAttacks(WorldCore::FMapInstanceId mapInstanceId,
			std::span<const WorldCore::SPlayerAttackRequestIdentity> attackRequests,
			EWorldResultCode resultCode,
			std::uint64_t serverTick,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void CompleteRejectedAttacks(WorldCore::FMapInstanceId mapInstanceId,
			std::span<const WorldCore::SRejectedPlayerAttack> rejectedAttacks,
			std::uint64_t serverTick,
			ContentsRuntime::Bridge::IContentBridge& bridge);
		void RemoveDisconnectedSessions(ContentsRuntime::Bridge::IContentBridge& bridge);
		[[nodiscard]] bool RemoveLocalPlayer(std::uint64_t sessionId, ContentsRuntime::Bridge::IContentBridge& bridge);
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
		std::uint32_t m_shardIndex = 0;
		std::uint32_t m_shardCount = 0;
		std::uint32_t m_targetFps = 1;
		std::uint64_t m_maxPacketQueueDepth = 0;
		std::shared_ptr<FWorldSessionRegistry> m_sessionRegistry;
		std::shared_ptr<FTaskGraphSectorExecutionService> m_taskGraphExecutionService;
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		SCachePresenceConfig m_cachePresenceConfig;
		std::shared_ptr<const Domain::FPlayerStatCalculator> m_playerStatCalculator;
		RpcLib::Dispatch::FRpcMethodDispatcher m_rpcDispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
		std::unique_ptr<WorldCore::FMapInstanceManager> m_mapInstanceManager;
		std::unordered_map<WorldCore::FMapDataId, WorldCore::FMapInstanceId> m_mapInstanceIdsByMapDataId;
		std::unordered_map<std::uint64_t, std::uint64_t> m_sessionGenerations;
		std::unordered_map<std::uint64_t, SLocalSessionState> m_localSessions;
		std::unordered_map<WorldCore::FEntityId, std::uint64_t> m_sessionIdsByEntityId;
		std::unordered_map<std::uint64_t, SDeferredMapEnter> m_deferredMapEnters;
		std::unordered_map<std::uint64_t, std::uint64_t> m_deferredLeaveGenerations;
		std::unordered_map<std::uint64_t, SPendingEquipmentMutation> m_pendingEquipmentMutations;
	};
}
