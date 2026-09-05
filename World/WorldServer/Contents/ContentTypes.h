#pragma once

namespace WorldServer::Contents
{
	inline constexpr ContentsRuntime::Core::FContentId kMapRouterContentId = 1;
	inline constexpr ContentsRuntime::Core::FContentId kMapContentId = 2;
	inline constexpr ContentsRuntime::Core::FContentId kSectorExecutorContentId = 3;
	inline constexpr std::uint64_t kDefaultMonsterSpawnSeed = 0x4D4F4E5354455255ull;

	enum class EWorldResultCode : std::uint16_t
	{
		Success = 0,
		InvalidRequest = 1,
		RequestAlreadyProcessing = 2,
		SessionUnavailable = 3,
		MapNotFound = 4,
		RouteFailure = 5,
		ServerBusy = 6,
		AlreadyInMap = 7,
		EntityRegistrationFailed = 8,
		MoveRejected = 9,
		InternalError = 10,
		MoveSuperseded = 11,
		PlayerNotReady = 12,
		CacheUnavailable = 13,
		PlayerRevoked = 14,
		AuthRequired = 15,
		AuthenticationFailed = 16,
		AlreadyAuthenticated = 17,
		ConcurrentModification = 18,
		ItemNotFound = 19,
		ItemVersionMismatch = 20,
		NotEquipment = 21,
		EquipmentStateConflict = 22,
		AttackRejected = 23,
		AttackAttackerDead = 24,
		AttackTargetInvalid = 25,
		AttackTargetDead = 26,
		AttackOutOfRange = 27,
		AttackCooldown = 28
	};

	struct SBootMapDefinition final
	{
		WorldCore::FMapDataId mapDataId = WorldCore::kInvalidMapDataId;
		WorldCore::FMapInstanceId mapInstanceId = WorldCore::kInvalidMapInstanceId;
		WorldCore::SMapDefinition definition;
		std::vector<WorldCore::SMonsterSpawnerRuntimeDefinition> monsterSpawners;
	};

	struct SMapRoute final
	{
		WorldCore::FMapDataId mapDataId = WorldCore::kInvalidMapDataId;
		WorldCore::FMapInstanceId mapInstanceId = WorldCore::kInvalidMapInstanceId;
		std::uint32_t shardIndex = 0;
		ContentsRuntime::Core::FContentInstanceId contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
	};

	struct SCachePresenceConfig final
	{
		bool enabled = false;
		std::uint32_t cacheServerInstanceId = 0;
		std::chrono::milliseconds rpcTimeout{3000};
		std::chrono::milliseconds retryInterval{1000};
	};

	struct SWorldAuthConfig final
	{
		bool enabled = false;
		std::uint32_t worldServerInstanceId = 0;
	};

	inline constexpr std::size_t GetMapContentShardIndex(
		const WorldCore::FMapInstanceId mapInstanceId,
		const std::size_t shardCount) noexcept
	{
		return shardCount == 0 ? 0 : static_cast<std::size_t>(mapInstanceId % shardCount);
	}
}
