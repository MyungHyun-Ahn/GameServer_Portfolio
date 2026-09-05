#pragma once

namespace WorldCore
{
	using FMapDataId = std::uint32_t;
	using FMapInstanceId = std::uint64_t;
	using FEntityId = std::uint64_t;
	using FUserId = std::uint64_t;
	using FMonsterDataId = std::uint32_t;
	using FSpawnerDataId = std::uint32_t;
	using FSpawnGeneration = std::uint64_t;
	using FSectorId = std::uint32_t;
	using FMoveSequence = std::uint32_t;
	using FAttackSequence = std::uint32_t;
	using FMapIncarnation = std::uint64_t;
	using FMapTickGeneration = std::uint64_t;

	inline constexpr FMapDataId kInvalidMapDataId = 0;
	inline constexpr FMapInstanceId kInvalidMapInstanceId = 0;
	inline constexpr FEntityId kInvalidEntityId = 0;
	inline constexpr FUserId kInvalidUserId = 0;
	inline constexpr FMonsterDataId kInvalidMonsterDataId = 0;
	inline constexpr FSpawnerDataId kInvalidSpawnerDataId = 0;
	inline constexpr FSpawnGeneration kInvalidSpawnGeneration = 0;
	inline constexpr FSectorId kInvalidSectorId = std::numeric_limits<FSectorId>::max();
	inline constexpr FMapIncarnation kInvalidMapIncarnation = 0;
	inline constexpr FMapTickGeneration kInvalidMapTickGeneration = 0;
	inline constexpr FEntityId kMapLocalEntityIdBit = FEntityId{1} << 63u;

	enum class ESectorExecutionMode : std::uint8_t
	{
		Serial = 1,
		TaskGraph = 2
	};

	enum class EActorKind : std::uint8_t
	{
		Player = 1,
		Monster = 2
	};

	enum class EMonsterType : std::uint8_t
	{
		Normal = 1,
		Boss = 2
	};

	enum class EMonsterAggroType : std::uint8_t
	{
		Aggressive = 1,
		Passive = 2
	};

	enum class EMonsterAiState : std::uint8_t
	{
		Idle = 1,
		Chase = 2,
		AttackReady = 3,
		Return = 4
	};

	enum class EPlayerLifeState : std::uint8_t
	{
		Alive = 1,
		Dead = 2
	};

	enum class ESectorExecutionResult : std::uint8_t
	{
		CompletedInline = 0,
		Pending = 1,
		Failed = 2
	};

	enum class EMapTickExecutionState : std::uint8_t
	{
		Idle = 0,
		Executing = 1,
		ReadyToCommit = 2,
		Failed = 3
	};

	enum class EMapTickResult : std::uint8_t
	{
		Completed = 0,
		Pending = 1,
		Failed = 2
	};

	enum class EMapTickCompletionStatus : std::uint8_t
	{
		Succeeded = 0,
		Failed = 1
	};

	enum class EMapTickCompletionResult : std::uint8_t
	{
		Accepted = 0,
		NotExecuting = 1,
		StaleTicket = 2,
		DuplicateCompletion = 3
	};

	enum class EMapCreateResult : std::uint8_t
	{
		Success = 0,
		InvalidDefinition = 1,
		DuplicateMapInstance = 2,
		UnsupportedExecutionMode = 3
	};

	enum class EMoveState : std::uint8_t
	{
		Start = 1,
		Sync = 2,
		Stop = 3
	};

	enum class EPlayerAttackRejectReason : std::uint8_t
	{
		InvalidAttacker = 1,
		AttackerDead = 2,
		InvalidTarget = 3,
		TargetDead = 4,
		OutOfRange = 5,
		Cooldown = 6
	};

	enum class EVisibilityEventKind : std::uint8_t
	{
		Spawn = 1,
		Despawn = 2,
		Move = 3
	};

	struct SVector2 final
	{
		float x = 0.0f;
		float y = 0.0f;

		bool operator==(const SVector2&) const noexcept = default;
	};

	struct SPlayerRuntimeSnapshot final
	{
		std::uint64_t characterId = 0;
		std::uint32_t characterDataId = 0;
		std::uint32_t level = 0;
		std::uint64_t exp = 0;
		std::uint64_t requiredExpToNextLevel = 0;
		std::uint32_t str = 0;
		std::uint32_t dex = 0;
		std::uint32_t intelligence = 0;
		std::uint32_t luk = 0;
		std::uint32_t unspentStatPoints = 0;
		std::uint64_t progressVersion = 0;
		std::uint64_t statVersion = 0;
		std::uint32_t finalStr = 0;
		std::uint32_t finalDex = 0;
		std::uint32_t finalIntelligence = 0;
		std::uint32_t finalLuk = 0;
		std::uint32_t maxHp = 0;
		std::uint32_t maxMp = 0;
		std::uint32_t attack = 0;
		std::uint32_t defense = 0;
		std::uint32_t moveSpeedMilli = 0;
		float collisionRadius = 0.0f;
		std::uint64_t equipmentVersion = 0;
		std::uint64_t statRevision = 0;

		bool operator==(const SPlayerRuntimeSnapshot&) const noexcept = default;
	};

	struct SMonsterRuntimeSnapshot final
	{
		FMonsterDataId monsterDataId = kInvalidMonsterDataId;
		EMonsterType monsterType = EMonsterType::Normal;
		EMonsterAggroType aggroType = EMonsterAggroType::Aggressive;
		std::uint32_t maxHp = 0;
		std::uint32_t attack = 0;
		std::uint32_t defense = 0;
		float moveSpeed = 0.0f;
		float collisionRadius = 0.0f;
		float aggroRadius = 0.0f;
		float leashRadius = 0.0f;
		float attackRange = 0.0f;
		std::uint32_t attackCooldownMilliseconds = 0;
		std::uint64_t attackCooldownTicks = 0;

		bool operator==(const SMonsterRuntimeSnapshot&) const noexcept = default;
	};

	struct SCombatRuntimePolicy final
	{
		std::uint32_t minimumDamage = 0;
		float playerBasicAttackRange = 0.0f;
		std::uint32_t playerBasicAttackCooldownMilliseconds = 0;

		bool operator==(const SCombatRuntimePolicy&) const noexcept = default;
	};

	struct SMapDefinition final
	{
		FMapDataId mapDataId = kInvalidMapDataId;
		std::uint32_t worldWidth = 0;
		std::uint32_t worldHeight = 0;
		std::uint32_t sectorSize = 0;
		std::uint32_t visibilitySectorRadius = 1;
		SVector2 spawnPosition{};
		SVector2 playerSpawnAreaMinimum{};
		SVector2 playerSpawnAreaMaximum{};
		std::uint64_t playerRespawnDelayTicks = 0;
		float maxAcceptedPositionError = 64.0f;
		std::uint32_t tickRateHz = 20;
		ESectorExecutionMode sectorExecutionMode = ESectorExecutionMode::Serial;
		SCombatRuntimePolicy combatPolicy{};
	};

	struct SMonsterSpawnerRuntimeDefinition final
	{
		FSpawnerDataId spawnerDataId = kInvalidSpawnerDataId;
		FMapDataId mapDataId = kInvalidMapDataId;
		SMonsterRuntimeSnapshot monsterSnapshot{};
		SVector2 areaMinimum{};
		SVector2 areaMaximum{};
		std::uint32_t initialSpawnCount = 0;
		std::uint32_t maxAliveCount = 0;
		std::uint64_t respawnDelayTicks = 0;

		bool operator==(const SMonsterSpawnerRuntimeDefinition&) const noexcept = default;
	};

	struct SMoveCommand final
	{
		FEntityId entityId = kInvalidEntityId;
		FMoveSequence sequence = 0;
		EMoveState moveState = EMoveState::Stop;
		SVector2 clientPosition{};
		SVector2 direction{};
	};

	struct SMoveResult final
	{
		FEntityId entityId = kInvalidEntityId;
		FMoveSequence sequence = 0;
		EMoveState moveState = EMoveState::Stop;
		SVector2 acceptedPosition{};
		SVector2 direction{};
		FSectorId previousSectorId = kInvalidSectorId;
		FSectorId currentSectorId = kInvalidSectorId;
		bool isCorrected = false;
	};

	struct SPlayerAttackCommand final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FAttackSequence attackSequence = 0;
		FEntityId targetEntityId = kInvalidEntityId;
	};

	struct SPlayerAttackRequestIdentity final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FAttackSequence attackSequence = 0;

		bool operator==(const SPlayerAttackRequestIdentity&) const noexcept = default;
	};

	struct SRejectedPlayerAttack final
	{
		SPlayerAttackRequestIdentity request{};
		FEntityId targetEntityId = kInvalidEntityId;
		EPlayerAttackRejectReason reason = EPlayerAttackRejectReason::InvalidAttacker;

		bool operator==(const SRejectedPlayerAttack&) const noexcept = default;
	};

	struct SPlayerAttackIntent final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FAttackSequence attackSequence = 0;
		FEntityId targetEntityId = kInvalidEntityId;
		FSectorId attackerSectorId = kInvalidSectorId;
		std::uint64_t tickIndex = 0;
		std::uint64_t expectedNextAttackTick = 0;

		bool operator==(const SPlayerAttackIntent&) const noexcept = default;
	};

	struct SPlayerAttackResult final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FAttackSequence attackSequence = 0;
		FEntityId targetEntityId = kInvalidEntityId;
		std::uint32_t damage = 0;
		std::uint32_t targetCurrentHp = 0;
		std::uint32_t targetMaxHp = 0;

		bool operator==(const SPlayerAttackResult&) const noexcept = default;
	};

	struct SMonsterAiResult final
	{
		FEntityId entityId = kInvalidEntityId;
		FEntityId targetEntityId = kInvalidEntityId;
		EMonsterAiState aiState = EMonsterAiState::Idle;
		EMoveState moveState = EMoveState::Stop;
		SVector2 acceptedPosition{};
		SVector2 direction{};
		FSectorId previousSectorId = kInvalidSectorId;
		FSectorId currentSectorId = kInvalidSectorId;

		bool operator==(const SMonsterAiResult&) const noexcept = default;
	};

	struct SMonsterAttackIntent final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FEntityId targetEntityId = kInvalidEntityId;
		FSpawnGeneration attackerSpawnGeneration = kInvalidSpawnGeneration;
		FSectorId attackerSectorId = kInvalidSectorId;
		std::uint64_t tickIndex = 0;
		std::uint64_t expectedNextAttackTick = 0;

		bool operator==(const SMonsterAttackIntent&) const noexcept = default;
	};

	struct SActorAttackResult final
	{
		FEntityId attackerEntityId = kInvalidEntityId;
		FEntityId targetEntityId = kInvalidEntityId;
		std::uint32_t damage = 0;
		std::uint32_t targetCurrentHp = 0;
		std::uint32_t targetMaxHp = 0;

		bool operator==(const SActorAttackResult&) const noexcept = default;
	};

	struct SActorAttackEvent final
	{
		FEntityId observerEntityId = kInvalidEntityId;
		SActorAttackResult attack{};

		bool operator==(const SActorAttackEvent&) const noexcept = default;
	};

	struct SActorDeathResult final
	{
		FEntityId entityId = kInvalidEntityId;
		FEntityId killerEntityId = kInvalidEntityId;
		std::uint64_t lifeRevision = 0;
		std::uint64_t serverTick = 0;

		bool operator==(const SActorDeathResult&) const noexcept = default;
	};

	struct SActorDeathEvent final
	{
		FEntityId observerEntityId = kInvalidEntityId;
		SActorDeathResult death{};

		bool operator==(const SActorDeathEvent&) const noexcept = default;
	};

	struct SActorRespawnResult final
	{
		FEntityId entityId = kInvalidEntityId;
		SVector2 position{};
		SVector2 direction{};
		std::uint32_t currentHp = 0;
		std::uint32_t maxHp = 0;
		std::uint64_t lifeRevision = 0;
		std::uint64_t serverTick = 0;
		FSectorId previousSectorId = kInvalidSectorId;
		FSectorId currentSectorId = kInvalidSectorId;

		bool operator==(const SActorRespawnResult&) const noexcept = default;
	};

	struct SActorRespawnEvent final
	{
		FEntityId observerEntityId = kInvalidEntityId;
		SActorRespawnResult respawn{};

		bool operator==(const SActorRespawnEvent&) const noexcept = default;
	};

	struct SSectorTransferCommand final
	{
		FEntityId entityId = kInvalidEntityId;
		FSectorId sourceSectorId = kInvalidSectorId;
		FSectorId targetSectorId = kInvalidSectorId;
	};

	struct SSectorTask final
	{
		FSectorId sectorId = kInvalidSectorId;
		std::uint32_t stableOrder = 0;
		std::uint64_t tickIndex = 0;
		std::vector<SMoveCommand> moveCommands;
		std::vector<SPlayerAttackCommand> playerAttackCommands;
	};

	struct SSectorTaskOutput final
	{
		FSectorId sectorId = kInvalidSectorId;
		std::uint32_t stableOrder = 0;
		std::vector<SMoveResult> moveResults;
		std::vector<SMonsterAiResult> monsterAiResults;
		std::vector<SMonsterAttackIntent> monsterAttackIntents;
		std::vector<SPlayerAttackIntent> playerAttackIntents;
		std::vector<SRejectedPlayerAttack> rejectedPlayerAttacks;
		std::vector<SSectorTransferCommand> sectorTransfers;
	};

	struct SMapTickTicket final
	{
		FMapInstanceId mapInstanceId = kInvalidMapInstanceId;
		FMapIncarnation mapIncarnation = kInvalidMapIncarnation;
		std::uint64_t tickIndex = 0;
		FMapTickGeneration generation = kInvalidMapTickGeneration;

		bool operator==(const SMapTickTicket&) const noexcept = default;
	};

	struct SMoveRequestIdentity final
	{
		FEntityId entityId = kInvalidEntityId;
		FMoveSequence sequence = 0;

		bool operator==(const SMoveRequestIdentity&) const noexcept = default;
	};

	struct SSectorExecutionStartResult final
	{
		ESectorExecutionResult executionResult = ESectorExecutionResult::Failed;
		std::vector<SSectorTaskOutput> taskOutputs;
		std::string failureReason;
	};

	struct SMapTickExecutionCompletion final
	{
		SMapTickTicket ticket{};
		EMapTickCompletionStatus status = EMapTickCompletionStatus::Failed;
		std::vector<SSectorTaskOutput> taskOutputs;
		std::string failureReason;
	};

	struct SVisibilityEvent final
	{
		EVisibilityEventKind kind = EVisibilityEventKind::Spawn;
		FEntityId observerEntityId = kInvalidEntityId;
		FEntityId subjectEntityId = kInvalidEntityId;
		SVector2 position{};
		SVector2 direction{};
		FMoveSequence moveSequence = 0;
	};

	struct SMonsterSpawnResult final
	{
		FEntityId entityId = kInvalidEntityId;
		FMonsterDataId monsterDataId = kInvalidMonsterDataId;
		FSpawnerDataId spawnerDataId = kInvalidSpawnerDataId;
		FSpawnGeneration spawnGeneration = kInvalidSpawnGeneration;
		SVector2 position{};
		FSectorId sectorId = kInvalidSectorId;

		bool operator==(const SMonsterSpawnResult&) const noexcept = default;
	};

	struct SMapTickResult final
	{
		FMapInstanceId mapInstanceId = kInvalidMapInstanceId;
		std::uint64_t tickIndex = 0;
		FMapTickGeneration tickGeneration = kInvalidMapTickGeneration;
		EMapTickResult result = EMapTickResult::Failed;
		bool executionStarted = false;
		std::vector<SMoveRequestIdentity> consumedMoveRequests;
		std::vector<SPlayerAttackRequestIdentity> consumedAttackRequests;
		std::vector<SRejectedPlayerAttack> rejectedAttackRequests;
		std::vector<SMoveRequestIdentity> rejectedMoveRequests;
		std::vector<SMoveResult> moveResults;
		std::vector<SMonsterAiResult> monsterAiResults;
		std::vector<SActorAttackResult> actorAttackResults;
		std::vector<SPlayerAttackResult> playerAttackResults;
		std::vector<SActorAttackEvent> actorAttackEvents;
		std::vector<SActorDeathResult> actorDeathResults;
		std::vector<SActorDeathEvent> actorDeathEvents;
		std::vector<SActorRespawnResult> actorRespawnResults;
		std::vector<SActorRespawnEvent> actorRespawnEvents;
		std::vector<SMonsterSpawnResult> spawnResults;
		std::vector<SVisibilityEvent> visibilityEvents;
		std::string failureReason;
	};

	[[nodiscard]] bool IsFinite(const SVector2& value) noexcept;
	[[nodiscard]] float GetDistanceSquared(const SVector2& lhs, const SVector2& rhs) noexcept;
	[[nodiscard]] SVector2 NormalizeOrZero(const SVector2& value) noexcept;
	[[nodiscard]] bool IsValidPlayerRuntimeSnapshot(const SPlayerRuntimeSnapshot& snapshot, std::string& outError);
	[[nodiscard]] bool IsValidMonsterRuntimeSnapshot(const SMonsterRuntimeSnapshot& snapshot, std::string& outError);
	[[nodiscard]] bool IsValidMonsterSpawnerRuntimeDefinition(const SMonsterSpawnerRuntimeDefinition& definition,
		const SMapDefinition& mapDefinition,
		std::string& outError);
	[[nodiscard]] bool IsValidMapDefinition(const SMapDefinition& definition, std::string& outError);
}
