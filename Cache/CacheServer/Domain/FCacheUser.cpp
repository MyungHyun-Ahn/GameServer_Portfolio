#include "CacheServerPch.h"

#include "CacheServer/Domain/FCacheUser.h"

namespace CacheServer::Domain
{
	std::unique_ptr<FCacheUser> FCacheUser::Create(
		SPlayerCacheSnapshot&& snapshot,
		const FLoadedAt loadedAt,
		const FLastAccessedAt lastAccessedAt)
	{
		if (!IsValidSnapshot(snapshot))
		{
			return nullptr;
		}

		return std::unique_ptr<FCacheUser>(new FCacheUser(std::move(snapshot), loadedAt, lastAccessedAt));
	}

	FCacheUser::FCacheUser(
		SPlayerCacheSnapshot&& snapshot,
		const FLoadedAt loadedAt,
		const FLastAccessedAt lastAccessedAt)
		: m_userId(snapshot.userId)
		, m_progress(snapshot.progress)
		, m_currencies(std::move(snapshot.currencies))
		, m_inventoryItems(std::move(snapshot.inventoryItems))
		, m_loadedAt(loadedAt)
		, m_lastAccessedAt(lastAccessedAt)
	{
	}

	std::uint64_t FCacheUser::GetUserId() const noexcept
	{
		return m_userId;
	}

	std::size_t FCacheUser::GetCurrencyCount() const noexcept
	{
		return m_currencies.size();
	}

	std::size_t FCacheUser::GetInventoryItemCount() const noexcept
	{
		return m_inventoryItems.size();
	}

	bool FCacheUser::IsDataValid() const noexcept
	{
		return m_dataValid;
	}

	const FCachedCurrencyMap& FCacheUser::GetCurrencies() const noexcept
	{
		return m_currencies;
	}

	const FCachedInventoryItemMap& FCacheUser::GetInventoryItems() const noexcept
	{
		return m_inventoryItems;
	}

	const SPlayerProgress& FCacheUser::GetProgress() const noexcept
	{
		return m_progress;
	}

	const SPlayerStateRevision& FCacheUser::GetStateRevision() const noexcept
	{
		return m_stateRevision;
	}

	bool FCacheUser::ReplacePlayerData(
		SPlayerCacheSnapshot&& snapshot,
		const FLoadedAt loadedAt,
		const FLastAccessedAt lastAccessedAt)
	{
		if (snapshot.userId != m_userId || !IsValidSnapshot(snapshot))
		{
			return false;
		}

		m_progress = snapshot.progress;
		m_currencies = std::move(snapshot.currencies);
		m_inventoryItems = std::move(snapshot.inventoryItems);
		m_loadedAt = loadedAt;
		m_lastAccessedAt = lastAccessedAt;
		m_dataValid = true;
		return true;
	}

	void FCacheUser::InvalidatePlayerData(
		const FLastAccessedAt accessedAt) noexcept
	{
		m_currencies.clear();
		m_inventoryItems.clear();
		m_dataValid = false;
		m_lastAccessedAt = accessedAt;
	}

	void FCacheUser::UpsertCurrency(
		SCachedCurrency currency,
		const FLastAccessedAt accessedAt)
	{
		m_currencies.insert_or_assign(currency.currencyId, std::move(currency));
		m_lastAccessedAt = accessedAt;
	}

	void FCacheUser::UpsertInventoryItem(
		SCachedInventoryItem item,
		const FLastAccessedAt accessedAt)
	{
		m_inventoryItems.insert_or_assign(item.itemInstanceId, std::move(item));
		m_lastAccessedAt = accessedAt;
	}

	bool FCacheUser::RemoveInventoryItem(
		const std::uint64_t itemInstanceId,
		const FLastAccessedAt accessedAt) noexcept
	{
		m_lastAccessedAt = accessedAt;
		return m_inventoryItems.erase(itemInstanceId) != 0;
	}

	void FCacheUser::UpdateProgress(
		SPlayerProgress progress,
		const FLastAccessedAt accessedAt) noexcept
	{
		m_progress = progress;
		m_lastAccessedAt = accessedAt;
	}

	void FCacheUser::AdvanceStateRevision(
		const std::uint64_t equipmentVersion,
		const FLastAccessedAt accessedAt) noexcept
	{
		m_stateRevision.equipmentVersion = equipmentVersion;
		if (m_stateRevision.statRevision != std::numeric_limits<std::uint64_t>::max())
		{
			++m_stateRevision.statRevision;
		}

		m_lastAccessedAt = accessedAt;
	}

	FCacheUser::FLoadedAt FCacheUser::GetLoadedAt() const noexcept
	{
		return m_loadedAt;
	}

	FCacheUser::FLastAccessedAt FCacheUser::GetLastAccessedAt() const noexcept
	{
		return m_lastAccessedAt;
	}

	void FCacheUser::Touch(
		const FLastAccessedAt accessedAt) noexcept
	{
		m_lastAccessedAt = accessedAt;
	}

	bool FCacheUser::HasGameOwner() const noexcept
	{
		return m_gameOwner.has_value();
	}

	const SGameUserOwner* FCacheUser::GetGameOwner() const noexcept
	{
		return m_gameOwner.has_value() ? &*m_gameOwner : nullptr;
	}

	bool FCacheUser::IsSameGameOwner(
		const std::uint64_t rpcSessionId,
		const std::uint32_t gameServerInstanceId,
		const std::uint64_t gameClientSessionId) const noexcept
	{
		return m_gameOwner.has_value() && m_gameOwner->rpcSessionId == rpcSessionId &&
			   m_gameOwner->gameServerInstanceId == gameServerInstanceId && m_gameOwner->gameClientSessionId == gameClientSessionId;
	}

	bool FCacheUser::MatchesGameOwner(
		const std::uint64_t rpcSessionId,
		const std::uint32_t gameServerInstanceId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration) const noexcept
	{
		return IsSameGameOwner(rpcSessionId, gameServerInstanceId, gameClientSessionId) && m_gameOwner->ownerGeneration == ownerGeneration;
	}

	void FCacheUser::SetGameOwner(
		SGameUserOwner owner,
		const FLastAccessedAt accessedAt) noexcept
	{
		m_gameOwner = std::move(owner);
		m_lastAccessedAt = accessedAt;
	}

	bool FCacheUser::RenewGameOwner(
		const std::uint64_t rpcSessionId,
		const std::uint32_t gameServerInstanceId,
		const std::uint64_t gameClientSessionId,
		const std::uint64_t ownerGeneration,
		const FLastAccessedAt now,
		const std::chrono::steady_clock::time_point leaseExpiresAt) noexcept
	{
		if (!MatchesGameOwner(rpcSessionId, gameServerInstanceId, gameClientSessionId, ownerGeneration) ||
			m_gameOwner->leaseExpiresAt <= now || leaseExpiresAt <= now)
		{
			return false;
		}

		m_gameOwner->leaseExpiresAt = leaseExpiresAt;
		m_lastAccessedAt = now;
		return true;
	}

	std::optional<SGameUserOwner> FCacheUser::ClearGameOwner(
		const FLastAccessedAt accessedAt) noexcept
	{
		std::optional<SGameUserOwner> previousOwner = std::move(m_gameOwner);
		m_gameOwner.reset();
		m_lastAccessedAt = accessedAt;
		return previousOwner;
	}

	bool FCacheUser::IsValidSnapshot(
		const SPlayerCacheSnapshot& snapshot) noexcept
	{
		if (snapshot.userId == 0)
		{
			return false;
		}
		if (snapshot.progress.characterId == 0 || snapshot.progress.characterDataId == 0 || snapshot.progress.level == 0 ||
			snapshot.progress.progressVersion == 0 || snapshot.progress.statVersion == 0)
		{
			return false;
		}

		for (const auto& [currencyId, currency] : snapshot.currencies)
		{
			if (currencyId != currency.currencyId || currency.version == 0)
			{
				return false;
			}
		}

		for (const auto& [itemInstanceId, item] : snapshot.inventoryItems)
		{
			if (itemInstanceId == 0 || itemInstanceId != item.itemInstanceId || item.itemDataId == 0 || item.quantity == 0 ||
				item.itemDataJson.empty() || item.version == 0)
			{
				return false;
			}
		}

		return true;
	}
}
