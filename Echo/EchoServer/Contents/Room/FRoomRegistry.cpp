#include "EchoServerPch.h"

#include "EchoServer/Contents/Room/FRoomRegistry.h"
#include "EchoServer/Contents/Room/RoomFlowTypes.h"

namespace EchoServer::Contents
{
	void FRoomRegistry::Initialize(
		std::vector<SRoomInfoSnapshot> roomDefinitions)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		m_rooms.clear();
		m_sessionRooms.clear();
		for (SRoomInfoSnapshot& roomDefinition : roomDefinitions)
		{
			roomDefinition.participantCount = 0;
			roomDefinition.joinable = roomDefinition.capacity > 0;
			m_rooms.emplace(roomDefinition.roomId, SRoomState{roomDefinition});
		}
	}

	std::vector<SRoomInfoSnapshot> FRoomRegistry::GetRoomsSnapshot() const
	{
		std::lock_guard<std::mutex> lock(m_lock);
		std::vector<SRoomInfoSnapshot> snapshots;
		snapshots.reserve(m_rooms.size());
		for (const auto& [roomId, roomState] : m_rooms)
		{
			(void)roomId;
			SRoomInfoSnapshot snapshot = roomState.snapshot;
			snapshot.joinable = snapshot.participantCount < snapshot.capacity;
			snapshots.push_back(std::move(snapshot));
		}

		std::sort(snapshots.begin(),
			snapshots.end(),
			[](const SRoomInfoSnapshot& left, const SRoomInfoSnapshot& right)
			{
				return left.roomId < right.roomId;
			});
		return snapshots;
	}

	std::optional<SRoomInfoSnapshot> FRoomRegistry::FindRoom(
		const std::uint32_t roomId) const
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto roomIt = m_rooms.find(roomId);
		if (roomIt == m_rooms.end())
		{
			return std::nullopt;
		}

		SRoomInfoSnapshot snapshot = roomIt->second.snapshot;
		snapshot.joinable = snapshot.participantCount < snapshot.capacity;
		return snapshot;
	}

	std::optional<std::uint32_t> FRoomRegistry::GetSessionRoomId(
		const std::uint64_t sessionId) const
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto sessionIt = m_sessionRooms.find(sessionId);
		if (sessionIt == m_sessionRooms.end())
		{
			return std::nullopt;
		}

		return sessionIt->second.roomId;
	}

	ERoomFlowResultCode FRoomRegistry::TryEnterRoom(
		const std::uint64_t sessionId,
		const std::uint32_t roomId,
		const std::uint64_t targetRouteGeneration,
		ContentsRuntime::Core::FContentInstanceId& outContentInstanceId)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		outContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;

		const auto roomIt = m_rooms.find(roomId);
		if (roomIt == m_rooms.end())
		{
			return ERoomFlowResultCode::InvalidRoomId;
		}

		if (roomIt->second.snapshot.contentInstanceId == ContentsRuntime::Core::kInvalidContentInstanceId)
		{
			return ERoomFlowResultCode::MissingContentInstance;
		}

		if (roomIt->second.snapshot.participantCount >= roomIt->second.snapshot.capacity)
		{
			return ERoomFlowResultCode::RoomFull;
		}

		m_sessionRooms[sessionId] = SSessionRoomState{roomId, targetRouteGeneration};
		++roomIt->second.snapshot.participantCount;
		roomIt->second.snapshot.joinable = roomIt->second.snapshot.participantCount < roomIt->second.snapshot.capacity;
		outContentInstanceId = roomIt->second.snapshot.contentInstanceId;
		return ERoomFlowResultCode::Success;
	}

	ERoomFlowResultCode FRoomRegistry::TryChangeRoom(
		const std::uint64_t sessionId,
		const std::uint32_t currentRoomId,
		const std::uint32_t targetRoomId,
		const std::uint64_t targetRouteGeneration,
		ContentsRuntime::Core::FContentInstanceId& outContentInstanceId)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		outContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;

		if (currentRoomId == targetRoomId)
		{
			return ERoomFlowResultCode::SameRoomNotAllowed;
		}

		const auto sessionIt = m_sessionRooms.find(sessionId);
		if (sessionIt == m_sessionRooms.end() || sessionIt->second.roomId != currentRoomId)
		{
			return ERoomFlowResultCode::RetryRequired;
		}

		const auto targetRoomIt = m_rooms.find(targetRoomId);
		if (targetRoomIt == m_rooms.end())
		{
			return ERoomFlowResultCode::InvalidRoomId;
		}

		if (targetRoomIt->second.snapshot.contentInstanceId == ContentsRuntime::Core::kInvalidContentInstanceId)
		{
			return ERoomFlowResultCode::MissingContentInstance;
		}

		if (targetRoomIt->second.snapshot.participantCount >= targetRoomIt->second.snapshot.capacity)
		{
			return ERoomFlowResultCode::RoomFull;
		}

		const auto currentRoomIt = m_rooms.find(currentRoomId);
		if (currentRoomIt == m_rooms.end())
		{
			return ERoomFlowResultCode::RetryRequired;
		}

		if (currentRoomIt->second.snapshot.participantCount > 0)
		{
			--currentRoomIt->second.snapshot.participantCount;
		}
		currentRoomIt->second.snapshot.joinable = currentRoomIt->second.snapshot.participantCount < currentRoomIt->second.snapshot.capacity;

		++targetRoomIt->second.snapshot.participantCount;
		targetRoomIt->second.snapshot.joinable = targetRoomIt->second.snapshot.participantCount < targetRoomIt->second.snapshot.capacity;
		sessionIt->second = SSessionRoomState{targetRoomId, targetRouteGeneration};
		outContentInstanceId = targetRoomIt->second.snapshot.contentInstanceId;
		return ERoomFlowResultCode::Success;
	}

	void FRoomRegistry::RevertEnterRoom(
		const std::uint64_t sessionId,
		const std::uint32_t roomId,
		const std::uint64_t targetRouteGeneration)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto sessionIt = m_sessionRooms.find(sessionId);
		if (sessionIt == m_sessionRooms.end() || sessionIt->second.roomId != roomId ||
			sessionIt->second.routeGeneration != targetRouteGeneration)
		{
			return;
		}

		const auto roomIt = m_rooms.find(roomId);
		if (roomIt != m_rooms.end() && roomIt->second.snapshot.participantCount > 0)
		{
			--roomIt->second.snapshot.participantCount;
			roomIt->second.snapshot.joinable = roomIt->second.snapshot.participantCount < roomIt->second.snapshot.capacity;
		}
		m_sessionRooms.erase(sessionIt);
	}

	void FRoomRegistry::RevertChangeRoom(
		const std::uint64_t sessionId,
		const std::uint32_t previousRoomId,
		const std::uint32_t targetRoomId,
		const std::uint64_t targetRouteGeneration)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto sessionIt = m_sessionRooms.find(sessionId);
		if (sessionIt == m_sessionRooms.end() || sessionIt->second.roomId != targetRoomId ||
			sessionIt->second.routeGeneration != targetRouteGeneration)
		{
			return;
		}

		const auto previousRoomIt = m_rooms.find(previousRoomId);
		const auto targetRoomIt = m_rooms.find(targetRoomId);
		if (targetRoomIt != m_rooms.end() && targetRoomIt->second.snapshot.participantCount > 0)
		{
			--targetRoomIt->second.snapshot.participantCount;
			targetRoomIt->second.snapshot.joinable =
				targetRoomIt->second.snapshot.participantCount < targetRoomIt->second.snapshot.capacity;
		}

		if (previousRoomIt != m_rooms.end())
		{
			++previousRoomIt->second.snapshot.participantCount;
			previousRoomIt->second.snapshot.joinable =
				previousRoomIt->second.snapshot.participantCount < previousRoomIt->second.snapshot.capacity;
		}

		sessionIt->second = SSessionRoomState{previousRoomId, targetRouteGeneration - 1};
	}

	void FRoomRegistry::LeaveRoom(
		const std::uint64_t sessionId,
		const std::uint32_t roomId,
		const std::uint64_t routeGeneration)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		const auto sessionIt = m_sessionRooms.find(sessionId);
		if (sessionIt == m_sessionRooms.end() || sessionIt->second.roomId != roomId || sessionIt->second.routeGeneration != routeGeneration)
		{
			return;
		}

		const auto roomIt = m_rooms.find(roomId);
		if (roomIt != m_rooms.end() && roomIt->second.snapshot.participantCount > 0)
		{
			--roomIt->second.snapshot.participantCount;
			roomIt->second.snapshot.joinable = roomIt->second.snapshot.participantCount < roomIt->second.snapshot.capacity;
		}
		m_sessionRooms.erase(sessionIt);
	}
}
