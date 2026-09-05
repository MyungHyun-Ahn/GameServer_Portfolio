#include "WorldServerPch.h"

#include "WorldServer/Contents/Session/FWorldSession.h"

namespace WorldServer::Contents
{
	FWorldSession::FWorldSession(
		const std::uint64_t sessionId) noexcept
		: FContentSession(sessionId)
	{
	}

	bool FWorldSession::TryBindAuthenticatedUser(
		const WorldCore::FUserId userId,
		const std::uint64_t loginVersion)
	{
		if (userId == WorldCore::kInvalidUserId)
		{
			return false;
		}

		std::lock_guard lock(m_playerLock);
		if (m_authenticated)
		{
			return false;
		}

		m_userId = userId;
		m_loginVersion = loginVersion;
		m_cacheOwnerGeneration = 0;
		m_playerRuntimeSnapshot.reset();
		m_authenticated = true;
		m_playerReady = false;
		return true;
	}

	bool FWorldSession::IsAuthenticated() const
	{
		std::lock_guard lock(m_playerLock);
		return m_authenticated;
	}

	std::uint64_t FWorldSession::GetLoginVersion() const
	{
		std::lock_guard lock(m_playerLock);
		return m_loginVersion;
	}

	bool FWorldSession::SetLegacyPlayerReady(
		const WorldCore::FUserId userId,
		const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot)
	{
		std::string snapshotError;
		if (userId == WorldCore::kInvalidUserId || !WorldCore::IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}

		std::lock_guard lock(m_playerLock);
		m_userId = userId;
		m_loginVersion = 0;
		m_cacheOwnerGeneration = 0;
		m_playerRuntimeSnapshot = runtimeSnapshot;
		m_authenticated = true;
		m_playerReady = true;
		return true;
	}

	bool FWorldSession::SetAuthenticatedPlayerReady(
		const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot)
	{
		std::string snapshotError;
		if (!WorldCore::IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}

		std::lock_guard lock(m_playerLock);
		if (!m_authenticated || m_userId == WorldCore::kInvalidUserId)
		{
			return false;
		}

		m_playerRuntimeSnapshot = runtimeSnapshot;
		m_cacheOwnerGeneration = 0;
		m_playerReady = true;
		return true;
	}

	bool FWorldSession::SetCachePlayerReady(
		const WorldCore::FUserId userId,
		const std::uint64_t ownerGeneration,
		const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot)
	{
		std::string snapshotError;
		if (userId == WorldCore::kInvalidUserId || ownerGeneration == 0 ||
			!WorldCore::IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}

		std::lock_guard lock(m_playerLock);
		if (!m_authenticated || m_userId != userId)
		{
			return false;
		}
		m_userId = userId;
		m_cacheOwnerGeneration = ownerGeneration;
		m_playerRuntimeSnapshot = runtimeSnapshot;
		m_playerReady = true;
		return true;
	}

	bool FWorldSession::TryApplyPlayerRuntimeSnapshot(
		const std::uint64_t expectedStatRevision,
		const std::uint64_t expectedEquipmentVersion,
		const WorldCore::SPlayerRuntimeSnapshot& runtimeSnapshot,
		const std::function<bool()>& applyMapSnapshot)
	{
		std::string snapshotError;
		if (expectedStatRevision == 0 || expectedEquipmentVersion == 0 || !applyMapSnapshot ||
			!WorldCore::IsValidPlayerRuntimeSnapshot(runtimeSnapshot, snapshotError))
		{
			return false;
		}

		std::lock_guard lock(m_playerLock);
		if (!m_authenticated || !m_playerReady || m_cacheOwnerGeneration == 0 || !m_playerRuntimeSnapshot.has_value() ||
			m_playerRuntimeSnapshot->statRevision != expectedStatRevision ||
			m_playerRuntimeSnapshot->equipmentVersion != expectedEquipmentVersion ||
			runtimeSnapshot.characterId != m_playerRuntimeSnapshot->characterId ||
			runtimeSnapshot.characterDataId != m_playerRuntimeSnapshot->characterDataId ||
			runtimeSnapshot.statRevision <= expectedStatRevision)
		{
			return false;
		}

		if (!applyMapSnapshot())
		{
			return false;
		}

		m_playerRuntimeSnapshot = runtimeSnapshot;
		return true;
	}

	void FWorldSession::ClearPlayerReady()
	{
		std::lock_guard lock(m_playerLock);
		m_playerReady = false;
		m_cacheOwnerGeneration = 0;
		m_playerRuntimeSnapshot.reset();
	}

	bool FWorldSession::IsPlayerReady() const
	{
		std::lock_guard lock(m_playerLock);
		return m_playerReady;
	}

	WorldCore::FUserId FWorldSession::GetUserId() const
	{
		std::lock_guard lock(m_playerLock);
		return m_userId;
	}

	std::uint64_t FWorldSession::GetCacheOwnerGeneration() const
	{
		std::lock_guard lock(m_playerLock);
		return m_cacheOwnerGeneration;
	}

	std::optional<WorldCore::SPlayerRuntimeSnapshot> FWorldSession::GetPlayerRuntimeSnapshot() const
	{
		std::lock_guard lock(m_playerLock);
		return m_playerRuntimeSnapshot;
	}

	bool FWorldSession::SetPendingMapEnter(
		const std::uint64_t requestId,
		const WorldCore::FMapDataId mapDataId,
		const ContentsRuntime::Session::FRequestProcessingToken& requestToken)
	{
		if (requestId == 0 || mapDataId == WorldCore::kInvalidMapDataId || !requestToken.IsValid())
		{
			return false;
		}

		std::lock_guard lock(m_pendingLock);
		if (m_pendingMapEnter.has_value())
		{
			return false;
		}

		m_pendingMapEnter = SPendingMapEnter{requestId, mapDataId, requestToken};
		return true;
	}

	std::optional<SPendingMapEnter> FWorldSession::FindPendingMapEnter(
		const std::uint64_t requestId,
		const WorldCore::FMapDataId mapDataId)
	{
		std::lock_guard lock(m_pendingLock);
		if (!m_pendingMapEnter.has_value() || m_pendingMapEnter->requestId != requestId || m_pendingMapEnter->mapDataId != mapDataId)
		{
			return std::nullopt;
		}

		return m_pendingMapEnter;
	}

	std::optional<SPendingMapEnter> FWorldSession::ConsumePendingMapEnter(
		const std::uint64_t requestId,
		const WorldCore::FMapDataId mapDataId)
	{
		std::lock_guard lock(m_pendingLock);
		if (!m_pendingMapEnter.has_value() || m_pendingMapEnter->requestId != requestId || m_pendingMapEnter->mapDataId != mapDataId)
		{
			return std::nullopt;
		}

		std::optional<SPendingMapEnter> result = std::move(m_pendingMapEnter);
		m_pendingMapEnter.reset();
		return result;
	}

	std::optional<SPendingMapEnter> FWorldSession::ConsumePendingMapEnter(
		const std::uint64_t requestId,
		const WorldCore::FMapDataId mapDataId,
		const ContentsRuntime::Session::FRequestProcessingToken& expectedToken)
	{
		std::lock_guard lock(m_pendingLock);
		if (!m_pendingMapEnter.has_value() || m_pendingMapEnter->requestId != requestId || m_pendingMapEnter->mapDataId != mapDataId ||
			m_pendingMapEnter->requestToken.sessionId != expectedToken.sessionId ||
			m_pendingMapEnter->requestToken.operationId != expectedToken.operationId)
		{
			return std::nullopt;
		}

		std::optional<SPendingMapEnter> result = std::move(m_pendingMapEnter);
		m_pendingMapEnter.reset();
		return result;
	}
}
