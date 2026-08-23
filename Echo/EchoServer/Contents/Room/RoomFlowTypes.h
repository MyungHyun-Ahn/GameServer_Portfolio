#pragma once

namespace EchoServer::Contents
{
	enum class ERoomFlowResultCode : std::uint16_t
	{
		Success = 0,
		InvalidRoomId = 1,
		SameRoomNotAllowed = 2,
		RoomFull = 3,
		RetryRequired = 4,
		MissingContentInstance = 100,
		RuntimeRouteFailure = 101,
		InternalError = 102
	};

	inline bool IsNormalRoomFlowFailure(
		const ERoomFlowResultCode resultCode) noexcept
	{
		switch (resultCode)
		{
			case ERoomFlowResultCode::InvalidRoomId:
			case ERoomFlowResultCode::SameRoomNotAllowed:
			case ERoomFlowResultCode::RoomFull:
			case ERoomFlowResultCode::RetryRequired:
				return true;
			default:
				return false;
		}
	}

	inline const char* ToString(
		const ERoomFlowResultCode resultCode) noexcept
	{
		switch (resultCode)
		{
			case ERoomFlowResultCode::Success:
				return "Success";
			case ERoomFlowResultCode::InvalidRoomId:
				return "InvalidRoomId";
			case ERoomFlowResultCode::SameRoomNotAllowed:
				return "SameRoomNotAllowed";
			case ERoomFlowResultCode::RoomFull:
				return "RoomFull";
			case ERoomFlowResultCode::RetryRequired:
				return "RetryRequired";
			case ERoomFlowResultCode::MissingContentInstance:
				return "MissingContentInstance";
			case ERoomFlowResultCode::RuntimeRouteFailure:
				return "RuntimeRouteFailure";
			case ERoomFlowResultCode::InternalError:
				return "InternalError";
			default:
				return "Unknown";
		}
	}
}
