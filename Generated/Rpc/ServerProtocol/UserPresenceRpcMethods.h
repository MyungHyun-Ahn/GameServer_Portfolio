#pragma once

// Generated from RPC YAML. Keep deterministic layout; do not format by hand.
// clang-format off
namespace ServerProtocol::UserPresence
{
	enum class EEnterUserResult : std::uint8_t;
	enum class ELeaveUserResult : std::uint8_t;
	enum class ERenewUserResult : std::uint8_t;
	enum class ERevokeUserReason : std::uint8_t;
	enum class ERevokeUserResult : std::uint8_t;

	using FUserId = std::uint64_t;
	using FLocalClientSessionId = std::uint64_t;
	using FOwnerGeneration = std::uint64_t;
	using FLeaseDurationMilliseconds = std::uint32_t;

	inline constexpr RpcLib::Protocol::FRpcServiceId kUserPresenceServiceId = 100;

	enum class EEnterUserResult : std::uint8_t
	{
		Entered = 0,
		AlreadyEntered = 1,
		ReplacedPreviousGameServer = 2,
		InvalidRequest = 3,
		UnauthorizedCaller = 4,
		UserLoadFailed = 5,
		ServerBusy = 6,
	};

	enum class ELeaveUserResult : std::uint8_t
	{
		Left = 0,
		AlreadyLeft = 1,
		StaleOwner = 2,
		InvalidRequest = 3,
		UnauthorizedCaller = 4,
	};

	enum class ERenewUserResult : std::uint8_t
	{
		Renewed = 0,
		OwnerNotFound = 1,
		StaleOwner = 2,
		InvalidRequest = 3,
		UnauthorizedCaller = 4,
	};

	enum class ERevokeUserReason : std::uint8_t
	{
		Unknown = 0,
		ReplacedByNewLogin = 1,
		LeaseExpired = 2,
	};

	enum class ERevokeUserResult : std::uint8_t
	{
		Revoked = 0,
		UserNotFound = 1,
		SessionMismatch = 2,
		StaleOwner = 3,
		InvalidRequest = 4,
		UnauthorizedCaller = 5,
	};

	struct FEnterUserRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kUserPresenceServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 1;
		static constexpr const char* kName = "UserPresence.EnterUser";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, FLocalClientSessionId>;
		using FResponseArguments = std::tuple<FUserId, EEnterUserResult, FOwnerGeneration, FLeaseDurationMilliseconds>;
	};

	struct FLeaveUserRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kUserPresenceServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 2;
		static constexpr const char* kName = "UserPresence.LeaveUser";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, FLocalClientSessionId, FOwnerGeneration>;
		using FResponseArguments = std::tuple<ELeaveUserResult>;
	};

	struct FRenewUserRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kUserPresenceServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 3;
		static constexpr const char* kName = "UserPresence.RenewUser";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, FLocalClientSessionId, FOwnerGeneration>;
		using FResponseArguments = std::tuple<ERenewUserResult>;
	};

	struct FRevokeUserRpc final
	{
		static constexpr RpcLib::Protocol::FRpcServiceId kServiceId = kUserPresenceServiceId;
		static constexpr RpcLib::Protocol::FRpcMethodId kMethodId = 4;
		static constexpr const char* kName = "UserPresence.RevokeUser";
		static constexpr bool kHasRoutingKey = true;
		static constexpr std::size_t kRoutingKeyArgumentIndex = 0;

		using FRequestArguments = std::tuple<FUserId, FLocalClientSessionId, FOwnerGeneration, ERevokeUserReason>;
		using FResponseArguments = std::tuple<ERevokeUserResult>;
	};
}
// clang-format on
