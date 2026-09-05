#pragma once

namespace WorldServer::Contents
{
	struct SPendingMapEnter final
	{
		std::uint64_t requestId = 0;
		WorldCore::FMapDataId mapDataId = WorldCore::kInvalidMapDataId;
		ContentsRuntime::Session::FRequestProcessingToken requestToken{};
	};

	class FWorldSession final : public ContentsRuntime::Session::FContentSession
	{
	public:
		explicit FWorldSession(std::uint64_t sessionId) noexcept;

		bool TryBindAuthenticatedUser(WorldCore::FUserId userId, std::uint64_t loginVersion);
		[[nodiscard]] bool IsAuthenticated() const;
		[[nodiscard]] std::uint64_t GetLoginVersion() const;
		bool SetLegacyPlayerReady(WorldCore::FUserId userId, const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot);
		bool SetAuthenticatedPlayerReady(const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot);
		bool SetCachePlayerReady(WorldCore::FUserId userId,
			std::uint64_t ownerGeneration,
			const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot);
		bool TryApplyPlayerRuntimeSnapshot(std::uint64_t expectedStatRevision,
			std::uint64_t expectedEquipmentVersion,
			const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot,
			const std::function<bool()>& applyMapSnapshot);
		void ClearPlayerReady();
		[[nodiscard]] bool IsPlayerReady() const;
		[[nodiscard]] WorldCore::FUserId GetUserId() const;
		[[nodiscard]] std::uint64_t GetCacheOwnerGeneration() const;
		[[nodiscard]] std::optional<WorldCore::SPlayerRuntimeSnapshot> GetPlayerRuntimeSnapshot() const;

		bool SetPendingMapEnter(std::uint64_t requestId,
			WorldCore::FMapDataId mapDataId,
			const ContentsRuntime::Session::FRequestProcessingToken& requestToken);
		std::optional<SPendingMapEnter> FindPendingMapEnter(std::uint64_t requestId, WorldCore::FMapDataId mapDataId);
		std::optional<SPendingMapEnter> ConsumePendingMapEnter(std::uint64_t requestId, WorldCore::FMapDataId mapDataId);
		std::optional<SPendingMapEnter> ConsumePendingMapEnter(std::uint64_t requestId,
			WorldCore::FMapDataId mapDataId,
			const ContentsRuntime::Session::FRequestProcessingToken& expectedToken);

	private:
		mutable std::mutex m_playerLock;
		WorldCore::FUserId m_userId = WorldCore::kInvalidUserId;
		std::uint64_t m_loginVersion = 0;
		std::uint64_t m_cacheOwnerGeneration = 0;
		std::optional<WorldCore::SPlayerRuntimeSnapshot> m_playerRuntimeSnapshot;
		bool m_authenticated = false;
		bool m_playerReady = false;
		std::mutex m_pendingLock;
		std::optional<SPendingMapEnter> m_pendingMapEnter;
	};
}
