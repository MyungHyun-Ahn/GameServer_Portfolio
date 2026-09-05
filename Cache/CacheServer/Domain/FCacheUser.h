#pragma once

namespace CacheServer::Domain
{
	struct SCachedCurrency
	{
		std::uint16_t currencyId = 0;
		std::uint64_t amount = 0;
		std::uint64_t version = 0;
	};

	struct SCachedInventoryItem
	{
		std::uint64_t itemInstanceId = 0;
		std::uint32_t itemDataId = 0;
		std::uint32_t quantity = 0;
		std::string itemDataJson;
		bool equipped = false;
		bool tradable = false;
		std::uint64_t version = 0;
	};

	struct SPrimaryStats
	{
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
	};

	struct SPlayerProgress
	{
		std::uint64_t characterId = 0;
		std::uint32_t characterDataId = 0;
		std::uint32_t level = 0;
		std::uint64_t exp = 0;
		SPrimaryStats persistentPrimary;
		std::uint32_t unspentStatPoints = 0;
		std::uint64_t progressVersion = 0;
		std::uint64_t statVersion = 0;
	};

	struct SPlayerStateRevision
	{
		std::uint64_t equipmentVersion = 0;
		std::uint64_t statRevision = 0;
	};

	using FCachedCurrencyMap = std::unordered_map<std::uint16_t, SCachedCurrency>;
	using FCachedInventoryItemMap = std::unordered_map<std::uint64_t, SCachedInventoryItem>;

	struct SPlayerCacheSnapshot
	{
		std::uint64_t userId = 0;
		SPlayerProgress progress;
		FCachedCurrencyMap currencies;
		FCachedInventoryItemMap inventoryItems;
	};

	struct SGameUserOwner
	{
		std::uint64_t rpcSessionId = 0;
		std::uint32_t gameServerInstanceId = 0;
		std::uint64_t gameClientSessionId = 0;
		std::uint64_t ownerGeneration = 0;
		std::chrono::steady_clock::time_point leaseExpiresAt{};
	};

	class FCacheUser final
	{
	public:
		using FLoadedAt = std::chrono::system_clock::time_point;
		using FLastAccessedAt = std::chrono::steady_clock::time_point;

		FCacheUser(const FCacheUser&) = delete;
		FCacheUser& operator=(const FCacheUser&) = delete;
		FCacheUser(FCacheUser&&) = delete;
		FCacheUser& operator=(FCacheUser&&) = delete;

		[[nodiscard]] static std::unique_ptr<FCacheUser> Create(SPlayerCacheSnapshot&& snapshot,
			FLoadedAt loadedAt,
			FLastAccessedAt lastAccessedAt);

		std::uint64_t GetUserId() const noexcept;
		std::size_t GetCurrencyCount() const noexcept;
		std::size_t GetInventoryItemCount() const noexcept;
		bool IsDataValid() const noexcept;
		const FCachedCurrencyMap& GetCurrencies() const noexcept;
		const FCachedInventoryItemMap& GetInventoryItems() const noexcept;
		const SPlayerProgress& GetProgress() const noexcept;
		const SPlayerStateRevision& GetStateRevision() const noexcept;
		bool ReplacePlayerData(SPlayerCacheSnapshot&& snapshot, FLoadedAt loadedAt, FLastAccessedAt lastAccessedAt);
		void InvalidatePlayerData(FLastAccessedAt accessedAt) noexcept;
		void UpsertCurrency(SCachedCurrency currency, FLastAccessedAt accessedAt);
		void UpsertInventoryItem(SCachedInventoryItem item, FLastAccessedAt accessedAt);
		bool RemoveInventoryItem(std::uint64_t itemInstanceId, FLastAccessedAt accessedAt) noexcept;
		void UpdateProgress(SPlayerProgress progress, FLastAccessedAt accessedAt) noexcept;
		void AdvanceStateRevision(std::uint64_t equipmentVersion, FLastAccessedAt accessedAt) noexcept;
		FLoadedAt GetLoadedAt() const noexcept;
		FLastAccessedAt GetLastAccessedAt() const noexcept;
		void Touch(FLastAccessedAt accessedAt) noexcept;
		bool HasGameOwner() const noexcept;
		const SGameUserOwner* GetGameOwner() const noexcept;
		bool IsSameGameOwner(std::uint64_t rpcSessionId,
			std::uint32_t gameServerInstanceId,
			std::uint64_t gameClientSessionId) const noexcept;
		bool MatchesGameOwner(std::uint64_t rpcSessionId,
			std::uint32_t gameServerInstanceId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration) const noexcept;
		void SetGameOwner(SGameUserOwner owner, FLastAccessedAt accessedAt) noexcept;
		bool RenewGameOwner(std::uint64_t rpcSessionId,
			std::uint32_t gameServerInstanceId,
			std::uint64_t gameClientSessionId,
			std::uint64_t ownerGeneration,
			FLastAccessedAt now,
			std::chrono::steady_clock::time_point leaseExpiresAt) noexcept;
		std::optional<SGameUserOwner> ClearGameOwner(FLastAccessedAt accessedAt) noexcept;

	private:
		FCacheUser(SPlayerCacheSnapshot&& snapshot, FLoadedAt loadedAt, FLastAccessedAt lastAccessedAt);
		static bool IsValidSnapshot(const SPlayerCacheSnapshot& snapshot) noexcept;

	private:
		std::uint64_t m_userId = 0;
		SPlayerProgress m_progress;
		SPlayerStateRevision m_stateRevision;
		FCachedCurrencyMap m_currencies;
		FCachedInventoryItemMap m_inventoryItems;
		bool m_dataValid = true;
		FLoadedAt m_loadedAt{};
		FLastAccessedAt m_lastAccessedAt{};
		std::optional<SGameUserOwner> m_gameOwner;
	};
}
