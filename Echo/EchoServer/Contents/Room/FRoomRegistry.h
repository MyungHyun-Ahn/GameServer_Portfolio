#pragma once

namespace EchoServer::Contents
{
	struct SRoomInfoSnapshot
	{
		std::uint32_t roomId = 0;
		ContentsRuntime::Core::FContentInstanceId contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::string roomName;
		std::uint32_t participantCount = 0;
		std::uint32_t capacity = 0;
		bool joinable = false;
	};

	class FRoomRegistry
	{
	public:
		void Initialize(std::vector<SRoomInfoSnapshot> roomDefinitions);
		std::vector<SRoomInfoSnapshot> GetRoomsSnapshot() const;
		std::optional<SRoomInfoSnapshot> FindRoom(std::uint32_t roomId) const;
		std::optional<std::uint32_t> GetSessionRoomId(std::uint64_t sessionId) const;
		ERoomFlowResultCode TryEnterRoom(std::uint64_t sessionId,
			std::uint32_t roomId,
			std::uint64_t targetRouteGeneration,
			ContentsRuntime::Core::FContentInstanceId& outContentInstanceId);
		ERoomFlowResultCode TryChangeRoom(std::uint64_t sessionId,
			std::uint32_t currentRoomId,
			std::uint32_t targetRoomId,
			std::uint64_t targetRouteGeneration,
			ContentsRuntime::Core::FContentInstanceId& outContentInstanceId);
		void RevertEnterRoom(std::uint64_t sessionId, std::uint32_t roomId, std::uint64_t targetRouteGeneration);
		void RevertChangeRoom(std::uint64_t sessionId,
			std::uint32_t previousRoomId,
			std::uint32_t targetRoomId,
			std::uint64_t targetRouteGeneration);
		void LeaveRoom(std::uint64_t sessionId, std::uint32_t roomId, std::uint64_t routeGeneration);

	private:
		struct SRoomState
		{
			SRoomInfoSnapshot snapshot;
		};

		struct SSessionRoomState
		{
			std::uint32_t roomId = 0;
			std::uint64_t routeGeneration = 0;
		};

	private:
		mutable std::mutex m_lock;
		std::unordered_map<std::uint32_t, SRoomState> m_rooms;
		std::unordered_map<std::uint64_t, SSessionRoomState> m_sessionRooms;
	};
}
