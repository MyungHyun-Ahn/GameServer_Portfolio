#pragma once

namespace RpcLib::Protocol
{
	using FRpcRequestId = std::uint64_t;
	using FRpcServiceId = std::uint32_t;
	using FRpcMethodId = std::uint32_t;
	using FRpcServerInstanceId = std::uint32_t;

	// Version 2 adds the one-way Notification wire opcode. Version 1 peers must not
	// become Ready because they cannot safely parse that opcode.
	inline constexpr std::uint32_t kRpcProtocolVersion = 2;
	// RIO accepts at most an 8 KiB framed send packet. Reserve 64 bytes for the
	// NetworkLib headers and RPC envelope so every backend shares one wire limit.
	inline constexpr std::size_t kMaxRpcPayloadBytes = (8u * 1024u) - 64u;
	inline constexpr std::uint32_t kMaxRpcCollectionElementCount = 65'536;

	enum class ERpcServerType : std::uint16_t
	{
		Unknown = 0,
		Auction = 1,
		Cache = 2,
		Login = 3,
		Game = 4
	};

	enum class ERpcSessionState : std::uint8_t
	{
		Connected = 0,
		Handshaking = 1,
		Ready = 2,
		Disconnected = 3
	};

	enum class ERpcHelloResult : std::uint16_t
	{
		Success = 0,
		InvalidServer = 1,
		ProtocolMismatch = 2,
		DuplicateServer = 3
	};

	enum class ERpcResponseCode : std::uint16_t
	{
		Success = 0,
		MethodNotFound = 1,
		InvalidPayload = 2,
		HandlerDidNotReply = 3,
		HandlerException = 4,
		ProtocolMismatch = 5,
		ServerBusy = 6,
		SessionNotReady = 7
	};

	enum class ERpcCallError : std::uint8_t
	{
		None = 0,
		InvalidArgument = 1,
		SessionUnavailable = 2,
		PendingLimitExceeded = 3,
		SerializationFailed = 4,
		SendFailed = 5,
		Timeout = 6,
		Disconnected = 7,
		RemoteError = 8,
		ProtocolError = 9
	};

	enum class ERpcCompletionResult : std::uint8_t
	{
		Completed = 0,
		NotFound = 1,
		OriginMismatch = 2,
		SessionMismatch = 3,
		ProtocolError = 4,
		RemoteError = 5
	};

	struct FRpcTarget final
	{
		ERpcServerType serverType = ERpcServerType::Unknown;
		std::uint64_t routingKey = 0;
		FRpcServerInstanceId serverInstanceId = 0;
		std::uint64_t rpcSessionId = 0;
	};

	struct FRpcCallStartResult final
	{
		bool accepted = false;
		FRpcRequestId requestId = 0;
		ERpcCallError error = ERpcCallError::None;
	};

	struct FRpcNotificationSendResult final
	{
		// accepted only confirms that the selected transport accepted this one-way send.
		// Notifications have no remote acknowledgement, timeout, or completion callback.
		bool accepted = false;
		std::uint64_t rpcSessionId = 0;
		ERpcCallError error = ERpcCallError::None;
	};

	// Local receive-side result. It can be logged by the receiver but is never sent back to the notifier.
	enum class ERpcNotificationDispatchResult : std::uint8_t
	{
		Dispatched = 0,
		SessionNotReady = 1,
		ProtocolMismatch = 2,
		MethodNotFound = 3,
		InvalidPayload = 4,
		HandlerException = 5
	};

	struct FRpcCallFailure final
	{
		ERpcCallError error = ERpcCallError::None;
		ERpcResponseCode remoteResponseCode = ERpcResponseCode::Success;
	};

	[[nodiscard]] constexpr bool IsRequestDefinitelyNotDispatched(
		const FRpcCallFailure& failure) noexcept
	{
		if (failure.error != ERpcCallError::RemoteError)
		{
			return false;
		}

		switch (failure.remoteResponseCode)
		{
			case ERpcResponseCode::MethodNotFound:
			case ERpcResponseCode::ProtocolMismatch:
			case ERpcResponseCode::SessionNotReady:
				return true;
			default:
				return false;
		}
	}
}
