#pragma once

namespace CacheServer::Contents
{
	class FPlayerCacheContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FPlayerCacheContent(std::shared_ptr<Foundation::ILogger> logger,
			ContentsRuntime::Core::FContentInstanceId contentInstanceId,
			std::uint32_t shardIndex,
			std::uint32_t shardCount,
			std::uint64_t maxPacketQueueDepth,
			RpcLib::Session::FRpcSessionRegistry& sessionRegistry,
			RpcLib::Call::FRpcRequestIdGenerator& requestIdGenerator,
			std::atomic<std::uint64_t>& ownerGenerationSequence,
			RpcLib::Transport::FServerRpcTransport& transport,
			Database::SCacheDatabaseConfig databaseConfig,
			std::shared_ptr<const GameData::Character::FCharacterDataTable> characterDataTable,
			std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> characterLevelDataTable,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
			std::shared_ptr<const GameData::Currency::FCurrencyDataTable> currencyDataTable,
			std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
			std::shared_ptr<const GameData::MailTemplate::FMailTemplateTable> mailTemplateTable,
			SPlayerCachePolicy cachePolicy,
			bool faultInjectionCreditBeforeDatabaseTransaction,
			bool faultInjectionCreditAfterCommitDisconnect,
			std::uint32_t faultInjectionCreditBeforeDatabaseDelayMilliseconds,
			std::uint32_t faultInjectionCreditAfterCommitDelayMilliseconds);

		ContentsRuntime::Core::FContentId GetContentId() const noexcept override;
		ContentsRuntime::Core::FContentInstanceId GetContentInstanceId() const noexcept override;
		std::uint64_t GetMaxPacketQueueDepth() const noexcept override;
		void OnEnter(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnLeave(std::uint64_t sessionId, std::uint64_t routeGeneration, ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnPacket(std::uint64_t sessionId,
			std::uint64_t routeGeneration,
			std::uint16_t opcode,
			std::span<const char> payload,
			ContentsRuntime::Bridge::IContentBridge& bridge) override;
		void OnFrame(int delayFrame, ContentsRuntime::Bridge::IContentBridge& bridge) override;

	private:
		void HandlePing(RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCachePingRpc>& reply,
			std::uint64_t sequence,
			std::uint64_t userId,
			std::uint64_t clientTimeUnixMs);
		void HandlePingNotification(const RpcLib::Dispatch::FRpcCallContext& context,
			std::uint64_t sequence,
			std::uint64_t userId,
			std::uint64_t clientTimeUnixMs);
		void HandleLoadUser(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FLoadCacheUserRpc>& reply,
			std::uint64_t sequence,
			std::uint64_t userId);
		void HandleGetInventory(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryRpc>& reply,
			std::uint64_t userId,
			std::uint64_t cursorItemInstanceId,
			std::uint32_t limit);
		void HandleGetMailList(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailListRpc>& reply,
			std::uint64_t userId,
			std::uint64_t cursorMailId,
			std::uint32_t limit);
		void HandleGetMailDetail(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetMailDetailRpc>& reply,
			std::uint64_t userId,
			std::uint64_t mailId);
		void HandleGetCurrency(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetCurrencyRpc>& reply,
			std::uint64_t userId,
			std::uint16_t currencyId);
		void HandleGetInventoryItem(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetInventoryItemRpc>& reply,
			std::uint64_t userId,
			std::uint64_t itemInstanceId);
		void HandleCreditCurrency(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreditCurrencyRpc>& reply,
			std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount);
		void HandleGrantInventoryItem(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantInventoryItemRpc>& reply,
			std::uint64_t userId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			std::uint32_t maxStack,
			std::uint32_t str,
			std::uint32_t dex,
			std::uint32_t intelligence,
			std::uint32_t luk,
			bool tradable);
		void HandleClaimMailAttachment(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FClaimMailAttachmentRpc>& reply,
			std::uint64_t userId,
			std::uint64_t mailId,
			std::uint64_t attachmentId);
		void HandleConsumeInventoryItemForListing(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FConsumeInventoryItemForListingRpc>& reply,
			std::uint64_t userId,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedVersion);
		void HandleDebitCurrency(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FDebitCurrencyRpc>& reply,
			std::uint64_t userId,
			std::uint16_t currencyId,
			std::uint64_t amount);
		void HandleSettleBuyout(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleBuyoutRpc>& reply,
			std::uint64_t buyerUserId,
			std::uint64_t sellerUserId,
			std::uint16_t currencyId,
			std::uint64_t additionalDebit,
			std::uint64_t buyoutPrice,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson);
		void HandleCreateListingReturnMail(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FCreateListingReturnMailRpc>& reply,
			std::uint64_t sellerUserId,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson);
		void HandleSettleExpiration(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FSettleExpirationRpc>& reply,
			std::uint64_t primaryUserId,
			std::uint64_t sellerUserId,
			std::uint64_t winnerUserId,
			std::uint16_t currencyId,
			std::uint64_t finalPrice,
			std::uint64_t itemInstanceId,
			std::uint32_t itemDataId,
			std::uint32_t quantity,
			const std::string& itemDataJson);
		void HandleGetPlayerWorldSnapshot(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGetPlayerWorldSnapshotRpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration);
		void HandleAllocatePlayerStat(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FAllocatePlayerStatRpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t expectedStatVersion,
			std::uint32_t addStr,
			std::uint32_t addDex,
			std::uint32_t addInt,
			std::uint32_t addLuk);
		void HandleGrantPlayerExperience(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FGrantPlayerExperienceRpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t expectedProgressVersion,
			std::uint64_t amount);
		void HandleEquipPlayerItem(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemRpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion);
		void HandleUnequipPlayerItem(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemRpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion);
		void HandleEquipPlayerItemV2(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FEquipPlayerItemV2Rpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion);
		void HandleUnequipPlayerItemV2(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<Cache::Protocol::FUnequipPlayerItemV2Rpc>& reply,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion);
		void SetPlayerEquipment(const RpcLib::Dispatch::FRpcCallContext& context,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			std::uint64_t itemInstanceId,
			std::uint64_t expectedItemVersion,
			std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion,
			bool equipped,
			Cache::Protocol::EPlayerEquipmentResult& outResult,
			Cache::Protocol::FPlayerWorldSnapshot& outSnapshot,
			std::uint64_t& outItemVersion,
			bool& outEquipped,
			bool& outStateInvalidated,
			std::string& outError);
		void HandleEnterUser(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FEnterUserRpc>& reply,
			std::uint64_t userId,
			std::uint64_t localClientSessionId);
		void HandleLeaveUser(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FLeaveUserRpc>& reply,
			std::uint64_t userId,
			std::uint64_t localClientSessionId,
			std::uint64_t ownerGeneration);
		void HandleRenewUser(const RpcLib::Dispatch::FRpcCallContext& context,
			RpcLib::Dispatch::TRpcReply<ServerProtocol::UserPresence::FRenewUserRpc>& reply,
			std::uint64_t userId,
			std::uint64_t localClientSessionId,
			std::uint64_t ownerGeneration);
		void SendRevokeUser(const Domain::SGameUserOwner& previousOwner,
			std::uint64_t userId,
			ServerProtocol::UserPresence::ERevokeUserReason reason);
		std::uint64_t NextOwnerGeneration() noexcept;
		bool IsAuthorizedGameCaller(const RpcLib::Dispatch::FRpcCallContext& context) const noexcept;
		bool IsAuthorizedDataQueryCaller(const RpcLib::Dispatch::FRpcCallContext& context) const noexcept;
		bool IsAuthorizedDataCommandCaller(const RpcLib::Dispatch::FRpcCallContext& context) const noexcept;
		bool IsAuthorizedAuctionCaller(const RpcLib::Dispatch::FRpcCallContext& context) const noexcept;
		bool IsCurrentGameOwner(const RpcLib::Dispatch::FRpcCallContext& context,
			const Domain::FCacheUser& user,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration) const noexcept;
		Domain::FCacheUser* GetCurrentGameOwnerUser(const RpcLib::Dispatch::FRpcCallContext& context,
			std::uint64_t userId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			Cache::Protocol::EPlayerProgressResult& outResult,
			std::string& outError);
		bool RefreshStateRevision(Domain::FCacheUser& user, std::string& outError) const;
		void RunMaintenance(std::chrono::steady_clock::time_point now);
		Domain::FCacheUser* FindUser(std::uint64_t userId) noexcept;
		Domain::FCacheUser* GetOrLoadUser(std::uint64_t userId,
			bool& outLoadedFromDatabase,
			Cache::Protocol::ECacheUserLoadResult& outResult,
			std::string& outError);
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
				m_logger->Log(level, "CacheServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		ContentsRuntime::Core::FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::uint32_t m_shardIndex = 0;
		std::uint32_t m_shardCount = 0;
		std::uint64_t m_maxPacketQueueDepth = 0;
		RpcLib::Session::FRpcSessionRegistry& m_sessionRegistry;
		std::atomic<std::uint64_t>& m_ownerGenerationSequence;
		RpcLib::Transport::FServerRpcTransport& m_transport;
		RpcLib::Dispatch::FRpcMethodDispatcher m_dispatcher;
		RpcLib::FRpcCommon m_rpcCommon;
		Database::SCacheDatabaseConfig m_databaseConfig;
		std::shared_ptr<const GameData::Character::FCharacterDataTable> m_characterDataTable;
		std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> m_characterLevelDataTable;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> m_inventoryPolicyTable;
		std::shared_ptr<const GameData::Currency::FCurrencyDataTable> m_currencyDataTable;
		std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> m_mailPolicyTable;
		std::shared_ptr<const GameData::MailTemplate::FMailTemplateTable> m_mailTemplateTable;
		SPlayerCachePolicy m_cachePolicy;
		bool m_faultInjectionCreditBeforeDatabaseTransaction = false;
		bool m_faultInjectionCreditAfterCommitDisconnect = false;
		std::uint32_t m_faultInjectionCreditBeforeDatabaseDelayMilliseconds = 0;
		std::uint32_t m_faultInjectionCreditAfterCommitDelayMilliseconds = 0;
		std::optional<RpcLib::Protocol::FRpcRequestId> m_disconnectBeforeResponseRequestId;
		std::unordered_map<std::uint64_t, std::unique_ptr<Domain::FCacheUser>> m_users;
		std::chrono::steady_clock::time_point m_nextMaintenanceAt{};
	};
}
