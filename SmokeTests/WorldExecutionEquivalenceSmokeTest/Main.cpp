#include "WorldExecutionEquivalenceSmokeTestPch.h"

#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "WorldServer/Contents/ContentTypes.h"
#include "WorldServer/Contents/Map/Sector/FSectorExecutorContent.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutionService.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutor.h"

namespace
{
	using ContentsRuntime::Core::FContentId;
	using ContentsRuntime::Core::FContentInstanceId;
	using WorldServer::Contents::FTaskGraphSectorExecutionService;
	using WorldServer::Contents::STaskGraphSectorExecutionStats;
	using namespace WorldCore;
	inline constexpr FEntityId kMonsterAiAnchorEntityId = 1;

	struct STestConfig final
	{
		std::string label;
		std::uint32_t workerCount = 4;
		std::uint32_t executorCount = 4;
		std::uint32_t pumpBatchSize = 1;
		std::uint32_t tickCount = 256;
		std::uint32_t playerCount = 96;
		std::uint32_t worldWidth = 128;
		std::uint32_t worldHeight = 128;
		std::uint32_t sectorSize = 8;
		std::uint32_t timeoutSeconds = 120;
		std::uint64_t seed = 0xD06E'CAFE'1234'5678ull;
	};

	struct STestReport final
	{
		bool passed = false;
		std::string failureReason;
		std::string label;
		std::uint32_t completedTickCount = 0;
		std::uint64_t finalStateHash = 0;
		std::uint64_t hashTraceDigest = 0;
		STaskGraphSectorExecutionStats taskGraphStats{};
	};

	std::uint64_t Mix64(
		std::uint64_t value) noexcept
	{
		value += 0x9E3779B97F4A7C15ull;
		value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
		return value ^ (value >> 31u);
	}

	void HashCombine(
		std::uint64_t& hash,
		const std::uint64_t value) noexcept
	{
		hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
	}

	bool HasSameBits(
		const float lhs,
		const float rhs) noexcept
	{
		return std::bit_cast<std::uint32_t>(lhs) == std::bit_cast<std::uint32_t>(rhs);
	}

	bool IsEqual(
		const SVector2& lhs,
		const SVector2& rhs) noexcept
	{
		return HasSameBits(lhs.x, rhs.x) && HasSameBits(lhs.y, rhs.y);
	}

	bool IsEqual(
		const SMoveResult& lhs,
		const SMoveResult& rhs) noexcept
	{
		return lhs.entityId == rhs.entityId && lhs.sequence == rhs.sequence && lhs.moveState == rhs.moveState &&
			   IsEqual(lhs.acceptedPosition, rhs.acceptedPosition) && IsEqual(lhs.direction, rhs.direction) &&
			   lhs.previousSectorId == rhs.previousSectorId && lhs.currentSectorId == rhs.currentSectorId &&
			   lhs.isCorrected == rhs.isCorrected;
	}

	bool IsEqual(
		const SVisibilityEvent& lhs,
		const SVisibilityEvent& rhs) noexcept
	{
		return lhs.kind == rhs.kind && lhs.observerEntityId == rhs.observerEntityId && lhs.subjectEntityId == rhs.subjectEntityId &&
			   IsEqual(lhs.position, rhs.position) && IsEqual(lhs.direction, rhs.direction) && lhs.moveSequence == rhs.moveSequence;
	}

	bool IsEqual(
		const SMonsterSpawnResult& lhs,
		const SMonsterSpawnResult& rhs) noexcept
	{
		return lhs.entityId == rhs.entityId && lhs.monsterDataId == rhs.monsterDataId && lhs.spawnerDataId == rhs.spawnerDataId &&
			   lhs.spawnGeneration == rhs.spawnGeneration && IsEqual(lhs.position, rhs.position) && lhs.sectorId == rhs.sectorId;
	}

	bool IsEqual(
		const SMonsterAiResult& lhs,
		const SMonsterAiResult& rhs) noexcept
	{
		return lhs.entityId == rhs.entityId && lhs.targetEntityId == rhs.targetEntityId && lhs.aiState == rhs.aiState &&
			   lhs.moveState == rhs.moveState && IsEqual(lhs.acceptedPosition, rhs.acceptedPosition) &&
			   IsEqual(lhs.direction, rhs.direction) && lhs.previousSectorId == rhs.previousSectorId &&
			   lhs.currentSectorId == rhs.currentSectorId;
	}

	std::string DescribeVector(
		const SVector2& value)
	{
		return std::format("({}, {}) [0x{:08X}, 0x{:08X}]",
			value.x,
			value.y,
			std::bit_cast<std::uint32_t>(value.x),
			std::bit_cast<std::uint32_t>(value.y));
	}

	class FEquivalenceOwnerContent final : public ContentsRuntime::Core::IContent
	{
	public:
		FEquivalenceOwnerContent(
			const FContentInstanceId contentInstanceId,
			std::shared_ptr<FTaskGraphSectorExecutionService> executionService,
			STestConfig config)
			: m_contentInstanceId(contentInstanceId)
			, m_executionService(std::move(executionService))
			, m_config(std::move(config))
		{
		}

		FContentId GetContentId() const noexcept override
		{
			return WorldServer::Contents::kMapContentId;
		}

		FContentInstanceId GetContentInstanceId() const noexcept override
		{
			return m_contentInstanceId;
		}

		std::uint32_t GetTargetFps() const noexcept override
		{
			return 60;
		}

		std::uint64_t GetMaxPacketQueueDepth() const noexcept override
		{
			return 4096;
		}

		void OnEnter(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnLeave(
			std::uint64_t,
			std::uint64_t,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnPacket(
			std::uint64_t,
			std::uint64_t,
			std::uint16_t,
			std::span<const char>,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
		}

		void OnFrame(
			int,
			ContentsRuntime::Bridge::IContentBridge&) override
		{
			m_executionService->DrainOwnerCallbacks(m_contentInstanceId);
		}

		void Start()
		{
			RunGuarded(
				[this]()
				{
					InitializeMaps();
					BeginNextTick();
				});
		}

		bool WaitForCompletion(
			const std::chrono::seconds timeout)
		{
			std::unique_lock lock(m_completionLock);
			return m_completionCondition.wait_for(lock,
				timeout,
				[this]()
				{
					return m_finished;
				});
		}

		void MarkTimedOut()
		{
			Fail("Timed out while waiting for the Serial/TaskGraph comparison to complete.");
		}

		STestReport BuildReport() const
		{
			std::lock_guard lock(m_completionLock);
			STestReport report{};
			report.passed = m_finished && m_passed;
			report.failureReason = m_failureReason;
			report.label = m_config.label;
			report.completedTickCount = m_completedTickCount;
			report.finalStateHash = m_finalStateHash;
			report.hashTraceDigest = m_hashTraceDigest;
			return report;
		}

	private:
		template <typename TCallback>
		void RunGuarded(
			TCallback&& callback)
		{
			try
			{
				callback();
			}
			catch (const std::exception& exception)
			{
				Fail(exception.what());
			}
			catch (...)
			{
				Fail("Unknown exception in equivalence owner callback.");
			}
		}

		void Require(
			const bool condition,
			std::string message) const
		{
			if (!condition)
			{
				throw std::runtime_error(std::move(message));
			}
		}

		void InitializeMaps()
		{
			SMapDefinition serialDefinition{};
			serialDefinition.mapDataId = 7001;
			serialDefinition.worldWidth = m_config.worldWidth;
			serialDefinition.worldHeight = m_config.worldHeight;
			serialDefinition.sectorSize = m_config.sectorSize;
			serialDefinition.visibilitySectorRadius = 1;
			serialDefinition.spawnPosition = {1.0f, 1.0f};
			serialDefinition.playerSpawnAreaMinimum = {0.0f, 0.0f};
			serialDefinition.playerSpawnAreaMaximum = {static_cast<float>(m_config.worldWidth), static_cast<float>(m_config.worldHeight)};
			serialDefinition.playerRespawnDelayTicks = 3;
			serialDefinition.maxAcceptedPositionError = static_cast<float>(std::max(m_config.worldWidth, m_config.worldHeight)) * 2.0f;
			serialDefinition.sectorExecutionMode = ESectorExecutionMode::Serial;
			serialDefinition.combatPolicy.minimumDamage = 1;
			serialDefinition.combatPolicy.playerBasicAttackRange = 64.0f;
			serialDefinition.combatPolicy.playerBasicAttackCooldownMilliseconds = 1'000;

			FMapInstanceFactory factory;
			EMapCreateResult createResult = EMapCreateResult::InvalidDefinition;
			std::string createError;
			constexpr FMapInstanceId mapInstanceId = 70'001;
			m_serialMap = factory.Create(mapInstanceId, serialDefinition, createResult, createError);
			Require(m_serialMap != nullptr && createResult == EMapCreateResult::Success,
				std::format("Serial Map creation failed: {}", createError));

			SMapDefinition taskGraphDefinition = serialDefinition;
			taskGraphDefinition.sectorExecutionMode = ESectorExecutionMode::TaskGraph;
			auto executor = std::make_unique<WorldServer::Contents::FTaskGraphSectorExecutor>(m_executionService,
				m_contentInstanceId,
				[this](SMapTickExecutionCompletion completion)
				{
					OnTaskGraphCompletion(std::move(completion));
				});
			m_taskGraphMap = factory.CreateWithExecutor(mapInstanceId, taskGraphDefinition, std::move(executor), createResult, createError);
			Require(m_taskGraphMap != nullptr && createResult == EMapCreateResult::Success,
				std::format("TaskGraph Map creation failed: {}", createError));

			const std::uint32_t columnCount = serialDefinition.worldWidth / serialDefinition.sectorSize;
			const std::uint32_t sectorCount = columnCount * (serialDefinition.worldHeight / serialDefinition.sectorSize);
			Require(m_config.playerCount <= sectorCount, "Player count must not exceed Sector count in this test.");
			m_entityIds.reserve(m_config.playerCount);
			m_nextMoveSequences.reserve(m_config.playerCount);
			m_nextAttackSequences.reserve(m_config.playerCount);
			for (std::uint32_t playerIndex = 0; playerIndex < m_config.playerCount; ++playerIndex)
			{
				const FEntityId entityId = static_cast<FEntityId>(playerIndex) + 1u;
				const std::uint32_t sectorId = (playerIndex * 37u) % sectorCount;
				const std::uint32_t sectorX = sectorId % columnCount;
				const std::uint32_t sectorY = sectorId / columnCount;
				const SVector2 position{static_cast<float>(sectorX * serialDefinition.sectorSize) + 0.5f,
					static_cast<float>(sectorY * serialDefinition.sectorSize) + 0.5f};
				const SVector2 direction = playerIndex % 2u == 0u ? SVector2{1.0f, 0.0f} : SVector2{0.0f, 1.0f};
				std::vector<SVisibilityEvent> serialEvents;
				std::vector<SVisibilityEvent> taskGraphEvents;
				std::string serialError;
				std::string taskGraphError;
				SPlayerRuntimeSnapshot playerSnapshot{};
				playerSnapshot.characterId = entityId + 200'000u;
				playerSnapshot.characterDataId = 1;
				playerSnapshot.level = 1;
				playerSnapshot.progressVersion = 1;
				playerSnapshot.statVersion = 1;
				playerSnapshot.maxHp = 1'000'000;
				playerSnapshot.maxMp = 100;
				playerSnapshot.attack = 50;
				playerSnapshot.defense = 4;
				playerSnapshot.moveSpeedMilli = 1'000;
				playerSnapshot.collisionRadius = 1.0f;
				playerSnapshot.equipmentVersion = 1;
				playerSnapshot.statRevision = 1;
				const bool serialAdded =
					m_serialMap->AddPlayer(entityId, entityId + 100'000u, position, direction, playerSnapshot, serialEvents, serialError);
				const bool taskGraphAdded = m_taskGraphMap->AddPlayer(
					entityId, entityId + 100'000u, position, direction, playerSnapshot, taskGraphEvents, taskGraphError);
				Require(serialAdded == taskGraphAdded && serialAdded,
					std::format("AddPlayer diverged. entity={} serial='{}' taskGraph='{}'", entityId, serialError, taskGraphError));
				CompareVisibilityEvents(serialEvents, taskGraphEvents, "AddPlayer");
				m_entityIds.push_back(entityId);
				m_nextMoveSequences.emplace(entityId, 0);
				m_nextAttackSequences.emplace(entityId, 0);
			}

			SMonsterRuntimeSnapshot monsterSnapshot{};
			monsterSnapshot.monsterDataId = 80'001;
			monsterSnapshot.monsterType = EMonsterType::Normal;
			monsterSnapshot.aggroType = EMonsterAggroType::Aggressive;
			monsterSnapshot.maxHp = 250;
			monsterSnapshot.attack = 15;
			monsterSnapshot.defense = 5;
			monsterSnapshot.moveSpeed = 3.5f;
			monsterSnapshot.collisionRadius = 0.2f;
			monsterSnapshot.aggroRadius = 6.0f;
			monsterSnapshot.leashRadius = 12.0f;
			monsterSnapshot.attackRange = 1.5f;
			monsterSnapshot.attackCooldownMilliseconds = 1'000;
			monsterSnapshot.attackCooldownTicks = 20;

			SMonsterSpawnerRuntimeDefinition spawner{};
			spawner.spawnerDataId = 90'001;
			spawner.mapDataId = serialDefinition.mapDataId;
			spawner.monsterSnapshot = monsterSnapshot;
			spawner.areaMinimum = {0.0f, 0.0f};
			// Entity 1 remains at (0.5, 0.5), so this compact Spawn area guarantees that the equivalence scenario
			// exercises Monster target acquisition and Chase instead of only comparing Idle output.
			spawner.areaMaximum = {4.0f, 4.0f};
			spawner.initialSpawnCount = 6;
			spawner.maxAliveCount = 6;
			spawner.respawnDelayTicks = 30;
			const std::array spawners{spawner};
			std::string serialSpawnError;
			std::string taskGraphSpawnError;
			const bool serialConfigured =
				m_serialMap->ConfigureMonsterSpawning(m_config.seed ^ 0x51A7'B00B'D15C'0DE5ull, spawners, serialSpawnError);
			const bool taskGraphConfigured =
				m_taskGraphMap->ConfigureMonsterSpawning(m_config.seed ^ 0x51A7'B00B'D15C'0DE5ull, spawners, taskGraphSpawnError);
			Require(serialConfigured == taskGraphConfigured && serialConfigured,
				std::format("Monster Spawn configuration diverged. serial='{}' taskGraph='{}'", serialSpawnError, taskGraphSpawnError));

			CompareMapState(0);
			m_hashTraceDigest = 0xCBF29CE484222325ull;
			HashCombine(m_hashTraceDigest, m_serialMap->GetStateHash());
		}

		SVector2 BuildPosition(
			const std::uint64_t random,
			const FEntityId entityId,
			const std::uint32_t tickIndex) const
		{
			if ((random % 97u) == 0u)
			{
				return {std::numeric_limits<float>::quiet_NaN(), static_cast<float>(entityId)};
			}
			if ((random % 31u) == 0u)
			{
				return {10'000.0f + static_cast<float>(entityId), -10'000.0f};
			}
			if ((random % 43u) == 0u)
			{
				return tickIndex % 2u == 0u
						   ? SVector2{-0.25f, static_cast<float>(random % m_config.worldHeight)}
						   : SVector2{static_cast<float>(m_config.worldWidth) + 0.25f, static_cast<float>(random % m_config.worldHeight)};
			}

			const std::uint32_t xUnit = static_cast<std::uint32_t>(random % (m_config.worldWidth * 1024u));
			const std::uint32_t yUnit = static_cast<std::uint32_t>((random >> 24u) % (m_config.worldHeight * 1024u));
			return {static_cast<float>(xUnit) / 1024.0f, static_cast<float>(yUnit) / 1024.0f};
		}

		SVector2 BuildDirection(
			const std::uint64_t random) const
		{
			if ((random % 89u) == 0u)
			{
				return {std::numeric_limits<float>::quiet_NaN(), 1.0f};
			}

			const std::int32_t x = static_cast<std::int32_t>((random >> 8u) % 7u) - 3;
			std::int32_t y = static_cast<std::int32_t>((random >> 16u) % 7u) - 3;
			if (x == 0 && y == 0)
			{
				y = 1;
			}
			return {static_cast<float>(x), static_cast<float>(y)};
		}

		void QueueSameMoves(
			const std::uint32_t tickIndex)
		{
			std::size_t expectedPendingMoveCount = 0;
			const bool emptyTick = tickIndex % 53u == 0u;
			for (const FEntityId entityId : m_entityIds)
			{
				if (entityId == kMonsterAiAnchorEntityId)
				{
					if (tickIndex == 64u || tickIndex == 160u)
					{
						++expectedPendingMoveCount;
						FMoveSequence& nextSequence = m_nextMoveSequences.at(entityId);
						SMoveCommand anchorCommand{};
						anchorCommand.entityId = entityId;
						anchorCommand.sequence = ++nextSequence;
						anchorCommand.moveState = EMoveState::Sync;
						anchorCommand.clientPosition = tickIndex == 64u ? SVector2{32.0f, 32.0f} : SVector2{0.5f, 0.5f};
						anchorCommand.direction = tickIndex == 64u ? SVector2{1.0f, 1.0f} : SVector2{-1.0f, -1.0f};
						QueueAndCompare(anchorCommand, true, "Monster AI anchor movement");
					}
					continue;
				}
				const std::uint64_t random =
					Mix64(m_config.seed ^ (static_cast<std::uint64_t>(tickIndex) << 32u) ^ (entityId * 0xD6E8FEB86659FD93ull));
				if (emptyTick || random % 5u == 0u)
				{
					continue;
				}

				++expectedPendingMoveCount;
				FMoveSequence& nextSequence = m_nextMoveSequences.at(entityId);
				SMoveCommand command{};
				command.entityId = entityId;
				command.sequence = ++nextSequence;
				command.moveState = static_cast<EMoveState>((tickIndex + entityId) % 3u + 1u);
				command.clientPosition = BuildPosition(random, entityId, tickIndex);
				command.direction = BuildDirection(random);
				QueueAndCompare(command, true, "primary input");

				if (random % 7u == 0u)
				{
					SMoveCommand replacement = command;
					replacement.sequence = ++nextSequence;
					const std::uint64_t replacementRandom = Mix64(random ^ 0xA0761D6478BD642Full);
					replacement.clientPosition = BuildPosition(replacementRandom, entityId, tickIndex);
					replacement.direction = BuildDirection(replacementRandom);
					QueueAndCompare(replacement, true, "latest-wins replacement");
					QueueAndCompare(command, false, "stale input rejection");
				}
			}

			Require(m_serialMap->GetPendingMoveCount() == expectedPendingMoveCount,
				std::format("Serial pending move count mismatch. tick={} expected={} actual={}",
					tickIndex,
					expectedPendingMoveCount,
					m_serialMap->GetPendingMoveCount()));
			Require(m_taskGraphMap->GetPendingMoveCount() == expectedPendingMoveCount,
				std::format("TaskGraph pending move count mismatch. tick={} expected={} actual={}",
					tickIndex,
					expectedPendingMoveCount,
					m_taskGraphMap->GetPendingMoveCount()));
		}

		void QueueAndCompare(
			const SMoveCommand& command,
			const bool expectedResult,
			const std::string_view context)
		{
			const bool serialResult = m_serialMap->QueueMove(command);
			const bool taskGraphResult = m_taskGraphMap->QueueMove(command);
			Require(serialResult == taskGraphResult && serialResult == expectedResult,
				std::format("QueueMove diverged in {}. tick={} entity={} sequence={} serial={} taskGraph={} expected={}",
					context,
					m_completedTickCount + 1,
					command.entityId,
					command.sequence,
					serialResult,
					taskGraphResult,
					expectedResult));
		}

		void QueueSameAttacks()
		{
			constexpr FEntityId attackerEntityId = 1;
			FEntityId targetEntityId = kInvalidEntityId;
			for (FEntityId sequence = 1; sequence <= 128; ++sequence)
			{
				const FEntityId candidate = kMapLocalEntityIdBit | sequence;
				if (m_serialMap->FindMonster(candidate) != nullptr)
				{
					targetEntityId = candidate;
					break;
				}
			}
			if (targetEntityId == kInvalidEntityId)
			{
				return;
			}

			FAttackSequence& nextSequence = m_nextAttackSequences.at(attackerEntityId);
			const SPlayerAttackCommand command{attackerEntityId, ++nextSequence, targetEntityId};
			const bool serialQueued = m_serialMap->QueuePlayerAttack(command);
			const bool taskGraphQueued = m_taskGraphMap->QueuePlayerAttack(command);
			Require(serialQueued == taskGraphQueued && serialQueued,
				std::format("QueuePlayerAttack diverged. tick={} sequence={} target={} serial={} taskGraph={}",
					m_completedTickCount + 1,
					command.attackSequence,
					command.targetEntityId,
					serialQueued,
					taskGraphQueued));
			Require(m_serialMap->GetPendingAttackCount() == 1 && m_taskGraphMap->GetPendingAttackCount() == 1,
				"Pending Player attack count diverged.");
			if ((command.attackSequence % 11u) == 0u)
			{
				Require(!m_serialMap->QueuePlayerAttack(command) && !m_taskGraphMap->QueuePlayerAttack(command),
					"Duplicate Player attack Sequence was accepted by an execution policy.");
			}
		}

		void BeginNextTick()
		{
			{
				std::lock_guard lock(m_completionLock);
				if (m_finished)
				{
					return;
				}
			}
			if (m_completedTickCount >= m_config.tickCount)
			{
				FinishSuccessfully();
				return;
			}

			const std::uint32_t tickIndex = m_completedTickCount + 1;
			QueueSameMoves(tickIndex);
			QueueSameAttacks();
			m_pendingSerialResult = m_serialMap->Tick();
			m_pendingTaskGraphStart = m_taskGraphMap->Tick();
			Require(m_pendingSerialResult.result == EMapTickResult::Completed && m_pendingSerialResult.executionStarted,
				std::format("Serial Tick did not complete inline. tick={} result={} reason='{}'",
					tickIndex,
					static_cast<std::uint32_t>(m_pendingSerialResult.result),
					m_pendingSerialResult.failureReason));
			Require(m_pendingTaskGraphStart.result == EMapTickResult::Pending && m_pendingTaskGraphStart.executionStarted,
				std::format("TaskGraph Tick did not enter Pending. tick={} result={} reason='{}'",
					tickIndex,
					static_cast<std::uint32_t>(m_pendingTaskGraphStart.result),
					m_pendingTaskGraphStart.failureReason));
			Require(m_serialMap->GetPendingMoveCount() == 0 && m_taskGraphMap->GetPendingMoveCount() == 0,
				std::format("Tick input snapshot was not consumed. tick={}", tickIndex));
			Require(m_taskGraphMap->GetTickExecutionState() == EMapTickExecutionState::Executing,
				std::format("TaskGraph Map is not Executing after Pending. tick={}", tickIndex));
			Require(m_pendingSerialResult.consumedMoveRequests == m_pendingTaskGraphStart.consumedMoveRequests,
				std::format("Consumed input identity diverged before TaskGraph completion. tick={}", tickIndex));
		}

		void OnTaskGraphCompletion(
			SMapTickExecutionCompletion completion)
		{
			RunGuarded(
				[this, completion = std::move(completion)]() mutable
				{
					const EMapTickCompletionResult completionResult = m_taskGraphMap->CompleteTickExecution(std::move(completion));
					Require(completionResult == EMapTickCompletionResult::Accepted,
						std::format("TaskGraph completion was rejected. tick={} result={}",
							m_completedTickCount + 1,
							static_cast<std::uint32_t>(completionResult)));
					const SMapTickResult taskGraphFinalResult = m_taskGraphMap->Tick();
					CompareCommittedTick(m_pendingSerialResult, m_pendingTaskGraphStart, taskGraphFinalResult);
					++m_completedTickCount;
					m_finalStateHash = m_serialMap->GetStateHash();
					HashCombine(m_hashTraceDigest, m_finalStateHash);
					HashCombine(m_hashTraceDigest, m_completedTickCount);
					BeginNextTick();
				});
		}

		void CompareCommittedTick(
			const SMapTickResult& serialResult,
			const SMapTickResult& taskGraphStart,
			const SMapTickResult& taskGraphFinal)
		{
			const std::uint32_t tickIndex = m_completedTickCount + 1;
			Require(taskGraphFinal.result == EMapTickResult::Completed && !taskGraphFinal.executionStarted,
				std::format("TaskGraph final Tick did not complete. tick={} result={} reason='{}'",
					tickIndex,
					static_cast<std::uint32_t>(taskGraphFinal.result),
					taskGraphFinal.failureReason));
			Require(serialResult.mapInstanceId == taskGraphStart.mapInstanceId &&
						serialResult.mapInstanceId == taskGraphFinal.mapInstanceId && serialResult.tickIndex == taskGraphStart.tickIndex &&
						serialResult.tickIndex == taskGraphFinal.tickIndex &&
						serialResult.tickGeneration == taskGraphStart.tickGeneration &&
						serialResult.tickGeneration == taskGraphFinal.tickGeneration,
				std::format("Tick identity diverged. logicalTick={}", tickIndex));
			Require(serialResult.consumedMoveRequests == taskGraphFinal.consumedMoveRequests,
				std::format("Consumed input identity diverged after TaskGraph completion. tick={}", tickIndex));
			Require(serialResult.rejectedMoveRequests == taskGraphFinal.rejectedMoveRequests,
				std::format("Rejected input identity diverged after TaskGraph completion. tick={}", tickIndex));
			Require(serialResult.consumedAttackRequests == taskGraphFinal.consumedAttackRequests,
				std::format("Consumed Player attack identity diverged after TaskGraph completion. tick={}", tickIndex));
			Require(taskGraphStart.consumedAttackRequests == taskGraphFinal.consumedAttackRequests,
				std::format("Pending TaskGraph start lost Player attack identities. tick={}", tickIndex));
			Require(serialResult.rejectedAttackRequests == taskGraphFinal.rejectedAttackRequests,
				std::format("Rejected Player attack identity diverged after TaskGraph completion. tick={}", tickIndex));
			Require(serialResult.playerAttackResults == taskGraphFinal.playerAttackResults,
				std::format("Player attack result diverged after TaskGraph completion. tick={}", tickIndex));
			CompareMoveResults(serialResult.moveResults, taskGraphFinal.moveResults, tickIndex);
			CompareMonsterAiResults(serialResult.monsterAiResults, taskGraphFinal.monsterAiResults, tickIndex);
			CompareActorAttackResults(serialResult.actorAttackResults, taskGraphFinal.actorAttackResults, tickIndex);
			CompareActorAttackEvents(serialResult.actorAttackEvents, taskGraphFinal.actorAttackEvents, tickIndex);
			CompareActorDeathResults(serialResult.actorDeathResults, taskGraphFinal.actorDeathResults, tickIndex);
			CompareActorDeathEvents(serialResult.actorDeathEvents, taskGraphFinal.actorDeathEvents, tickIndex);
			CompareActorRespawnResults(serialResult.actorRespawnResults, taskGraphFinal.actorRespawnResults, tickIndex);
			CompareActorRespawnEvents(serialResult.actorRespawnEvents, taskGraphFinal.actorRespawnEvents, tickIndex);
			CompareSpawnResults(serialResult.spawnResults, taskGraphFinal.spawnResults, tickIndex);
			CompareVisibilityEvents(serialResult.visibilityEvents, taskGraphFinal.visibilityEvents, "Tick");
			CompareMapState(tickIndex);
		}

		void CompareActorAttackResults(
			const std::span<const SActorAttackResult> serialResults,
			const std::span<const SActorAttackResult> taskGraphResults,
			const std::uint32_t tickIndex)
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Actor attack result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				Require(serialResults[index] == taskGraphResults[index],
					std::format("Actor attack result diverged. tick={} index={}", tickIndex, index));
			}
			m_observedMonsterAttack = m_observedMonsterAttack || !serialResults.empty();
		}

		void CompareActorAttackEvents(
			const std::span<const SActorAttackEvent> serialEvents,
			const std::span<const SActorAttackEvent> taskGraphEvents,
			const std::uint32_t tickIndex) const
		{
			Require(serialEvents.size() == taskGraphEvents.size(),
				std::format("Actor attack event count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialEvents.size(),
					taskGraphEvents.size()));
			for (std::size_t index = 0; index < serialEvents.size(); ++index)
			{
				Require(serialEvents[index] == taskGraphEvents[index],
					std::format("Actor attack event diverged. tick={} index={}", tickIndex, index));
			}
		}

		void CompareActorDeathResults(
			const std::span<const SActorDeathResult> serialResults,
			const std::span<const SActorDeathResult> taskGraphResults,
			const std::uint32_t tickIndex) const
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Actor Death result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				Require(serialResults[index] == taskGraphResults[index],
					std::format("Actor Death result diverged. tick={} index={}", tickIndex, index));
			}
		}

		void CompareActorDeathEvents(
			const std::span<const SActorDeathEvent> serialEvents,
			const std::span<const SActorDeathEvent> taskGraphEvents,
			const std::uint32_t tickIndex) const
		{
			Require(serialEvents.size() == taskGraphEvents.size(),
				std::format("Actor Death event count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialEvents.size(),
					taskGraphEvents.size()));
			for (std::size_t index = 0; index < serialEvents.size(); ++index)
			{
				Require(serialEvents[index] == taskGraphEvents[index],
					std::format("Actor Death event diverged. tick={} index={}", tickIndex, index));
			}
		}

		void CompareActorRespawnResults(
			const std::span<const SActorRespawnResult> serialResults,
			const std::span<const SActorRespawnResult> taskGraphResults,
			const std::uint32_t tickIndex) const
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Actor Respawn result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				Require(serialResults[index] == taskGraphResults[index],
					std::format("Actor Respawn result diverged. tick={} index={}", tickIndex, index));
			}
		}

		void CompareActorRespawnEvents(
			const std::span<const SActorRespawnEvent> serialEvents,
			const std::span<const SActorRespawnEvent> taskGraphEvents,
			const std::uint32_t tickIndex) const
		{
			Require(serialEvents.size() == taskGraphEvents.size(),
				std::format("Actor Respawn event count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialEvents.size(),
					taskGraphEvents.size()));
			for (std::size_t index = 0; index < serialEvents.size(); ++index)
			{
				Require(serialEvents[index] == taskGraphEvents[index],
					std::format("Actor Respawn event diverged. tick={} index={}", tickIndex, index));
			}
		}

		void CompareMonsterAiResults(
			const std::span<const SMonsterAiResult> serialResults,
			const std::span<const SMonsterAiResult> taskGraphResults,
			const std::uint32_t tickIndex)
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Monster AI result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				const SMonsterAiResult& serialResult = serialResults[index];
				const SMonsterAiResult& taskGraphResult = taskGraphResults[index];
				Require(IsEqual(serialResult, taskGraphResult),
					std::format("Monster AI result diverged. tick={} index={} entity={} serialTarget={} taskGraphTarget={} "
								"serialState={} taskGraphState={} serialMove={} taskGraphMove={} serialPos={} taskGraphPos={}",
						tickIndex,
						index,
						serialResult.entityId,
						serialResult.targetEntityId,
						taskGraphResult.targetEntityId,
						static_cast<std::uint32_t>(serialResult.aiState),
						static_cast<std::uint32_t>(taskGraphResult.aiState),
						static_cast<std::uint32_t>(serialResult.moveState),
						static_cast<std::uint32_t>(taskGraphResult.moveState),
						DescribeVector(serialResult.acceptedPosition),
						DescribeVector(taskGraphResult.acceptedPosition)));

				m_observedMonsterAiResult = true;
				m_observedMonsterTarget = m_observedMonsterTarget || serialResult.targetEntityId != kInvalidEntityId;
				m_observedMonsterChase = m_observedMonsterChase ||
										 (serialResult.aiState == EMonsterAiState::Chase && serialResult.moveState == EMoveState::Start);
				m_observedMonsterReturn = m_observedMonsterReturn ||
										  (serialResult.aiState == EMonsterAiState::Return && serialResult.moveState == EMoveState::Start);
			}
		}

		void CompareSpawnResults(
			const std::span<const SMonsterSpawnResult> serialResults,
			const std::span<const SMonsterSpawnResult> taskGraphResults,
			const std::uint32_t tickIndex) const
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Monster Spawn result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				Require(IsEqual(serialResults[index], taskGraphResults[index]),
					std::format("Monster Spawn result diverged. tick={} index={} serialEntity={} taskGraphEntity={}",
						tickIndex,
						index,
						serialResults[index].entityId,
						taskGraphResults[index].entityId));
			}
		}

		void CompareMoveResults(
			const std::span<const SMoveResult> serialResults,
			const std::span<const SMoveResult> taskGraphResults,
			const std::uint32_t tickIndex) const
		{
			Require(serialResults.size() == taskGraphResults.size(),
				std::format("Move result count diverged. tick={} serial={} taskGraph={}",
					tickIndex,
					serialResults.size(),
					taskGraphResults.size()));
			for (std::size_t index = 0; index < serialResults.size(); ++index)
			{
				Require(IsEqual(serialResults[index], taskGraphResults[index]),
					std::format("Move result diverged. tick={} index={} entity={} serialPos={} taskGraphPos={}",
						tickIndex,
						index,
						serialResults[index].entityId,
						DescribeVector(serialResults[index].acceptedPosition),
						DescribeVector(taskGraphResults[index].acceptedPosition)));
			}
		}

		void CompareVisibilityEvents(
			const std::span<const SVisibilityEvent> serialEvents,
			const std::span<const SVisibilityEvent> taskGraphEvents,
			const std::string_view context) const
		{
			Require(serialEvents.size() == taskGraphEvents.size(),
				std::format("{} visibility event count diverged. tick={} serial={} taskGraph={}",
					context,
					m_completedTickCount + 1,
					serialEvents.size(),
					taskGraphEvents.size()));
			for (std::size_t index = 0; index < serialEvents.size(); ++index)
			{
				const SVisibilityEvent& serialEvent = serialEvents[index];
				const SVisibilityEvent& taskGraphEvent = taskGraphEvents[index];
				Require(IsEqual(serialEvent, taskGraphEvent),
					std::format("{} visibility event diverged. tick={} index={} serial=({},{},{}) taskGraph=({},{},{})",
						context,
						m_completedTickCount + 1,
						index,
						static_cast<std::uint32_t>(serialEvent.kind),
						serialEvent.observerEntityId,
						serialEvent.subjectEntityId,
						static_cast<std::uint32_t>(taskGraphEvent.kind),
						taskGraphEvent.observerEntityId,
						taskGraphEvent.subjectEntityId));
			}
		}

		void CompareMapState(
			const std::uint32_t tickIndex) const
		{
			const std::uint64_t serialHash = m_serialMap->GetStateHash();
			const std::uint64_t taskGraphHash = m_taskGraphMap->GetStateHash();
			Require(serialHash == taskGraphHash,
				std::format("State hash diverged. tick={} serial=0x{:016X} taskGraph=0x{:016X}", tickIndex, serialHash, taskGraphHash));
			Require(m_serialMap->GetTickIndex() == m_taskGraphMap->GetTickIndex(),
				std::format("Committed Tick index diverged. tick={}", tickIndex));
			Require(m_serialMap->GetTickExecutionState() == EMapTickExecutionState::Idle &&
						m_taskGraphMap->GetTickExecutionState() == EMapTickExecutionState::Idle,
				std::format("Map did not return to Idle after Commit. tick={}", tickIndex));
			Require(m_serialMap->GetPlayerCount() == m_taskGraphMap->GetPlayerCount(),
				std::format("Player count diverged. tick={}", tickIndex));
			Require(m_serialMap->GetMonsterCount() == m_taskGraphMap->GetMonsterCount(),
				std::format("Monster count diverged. tick={}", tickIndex));
			Require(m_serialMap->GetPendingMoveCount() == m_taskGraphMap->GetPendingMoveCount(),
				std::format("Pending input count diverged. tick={}", tickIndex));
			Require(m_serialMap->GetPendingAttackCount() == m_taskGraphMap->GetPendingAttackCount(),
				std::format("Pending Player attack count diverged. tick={}", tickIndex));

			for (const FEntityId entityId : m_entityIds)
			{
				ComparePlayerState(entityId, tickIndex);
			}
			for (FEntityId monsterEntityId = kMapLocalEntityIdBit | 1u; monsterEntityId < (kMapLocalEntityIdBit | 7u); ++monsterEntityId)
			{
				CompareMonsterState(monsterEntityId, tickIndex);
			}

			const FSectorGrid& serialGrid = m_serialMap->GetSectorGrid();
			const FSectorGrid& taskGraphGrid = m_taskGraphMap->GetSectorGrid();
			Require(
				serialGrid.GetSectorCount() == taskGraphGrid.GetSectorCount(), std::format("Sector count diverged. tick={}", tickIndex));
			for (FSectorId sectorId = 0; sectorId < serialGrid.GetSectorCount(); ++sectorId)
			{
				const std::vector<FEntityId> serialEntities = serialGrid.GetEntityIdsInSector(sectorId);
				const std::vector<FEntityId> taskGraphEntities = taskGraphGrid.GetEntityIdsInSector(sectorId);
				Require(serialEntities == taskGraphEntities,
					std::format("Sector membership diverged. tick={} sector={} serialCount={} taskGraphCount={}",
						tickIndex,
						sectorId,
						serialEntities.size(),
						taskGraphEntities.size()));
			}
		}

		void CompareMonsterState(
			const FEntityId entityId,
			const std::uint32_t tickIndex) const
		{
			const FMonsterEntity* const serialMonster = m_serialMap->FindMonster(entityId);
			const FMonsterEntity* const taskGraphMonster = m_taskGraphMap->FindMonster(entityId);
			Require((serialMonster == nullptr) == (taskGraphMonster == nullptr),
				std::format("Monster presence diverged. tick={} entity={}", tickIndex, entityId));
			if (serialMonster == nullptr)
			{
				return;
			}
			const bool equal = serialMonster->GetRuntimeSnapshot() == taskGraphMonster->GetRuntimeSnapshot() &&
							   serialMonster->GetSpawnerDataId() == taskGraphMonster->GetSpawnerDataId() &&
							   serialMonster->GetSpawnGeneration() == taskGraphMonster->GetSpawnGeneration() &&
							   serialMonster->GetCurrentHp() == taskGraphMonster->GetCurrentHp() &&
							   IsEqual(serialMonster->GetSpawnPosition(), taskGraphMonster->GetSpawnPosition()) &&
							   serialMonster->GetTargetEntityId() == taskGraphMonster->GetTargetEntityId() &&
							   serialMonster->GetAiState() == taskGraphMonster->GetAiState() &&
							   serialMonster->GetMoveState() == taskGraphMonster->GetMoveState() &&
							   serialMonster->GetNextAttackTick() == taskGraphMonster->GetNextAttackTick() &&
							   serialMonster->GetSectorId() == taskGraphMonster->GetSectorId() &&
							   IsEqual(serialMonster->GetPosition(), taskGraphMonster->GetPosition()) &&
							   IsEqual(serialMonster->GetDirection(), taskGraphMonster->GetDirection());
			Require(equal, std::format("Monster state diverged. tick={} entity={}", tickIndex, entityId));
		}

		void ComparePlayerState(
			const FEntityId entityId,
			const std::uint32_t tickIndex) const
		{
			const FPlayerEntity* const serialPlayer = m_serialMap->FindPlayer(entityId);
			const FPlayerEntity* const taskGraphPlayer = m_taskGraphMap->FindPlayer(entityId);
			Require(serialPlayer != nullptr && taskGraphPlayer != nullptr,
				std::format("Player presence diverged. tick={} entity={}", tickIndex, entityId));
			const bool equal = serialPlayer->GetUserId() == taskGraphPlayer->GetUserId() &&
							   serialPlayer->GetSectorId() == taskGraphPlayer->GetSectorId() &&
							   serialPlayer->GetLastMoveSequence() == taskGraphPlayer->GetLastMoveSequence() &&
							   serialPlayer->GetMoveState() == taskGraphPlayer->GetMoveState() &&
							   serialPlayer->GetRuntimeSnapshot() == taskGraphPlayer->GetRuntimeSnapshot() &&
							   serialPlayer->GetCurrentHp() == taskGraphPlayer->GetCurrentHp() &&
							   serialPlayer->GetCurrentMp() == taskGraphPlayer->GetCurrentMp() &&
							   serialPlayer->GetLifeState() == taskGraphPlayer->GetLifeState() &&
							   serialPlayer->GetLifeRevision() == taskGraphPlayer->GetLifeRevision() &&
							   serialPlayer->GetRespawnDueTick() == taskGraphPlayer->GetRespawnDueTick() &&
							   serialPlayer->GetLastKillerEntityId() == taskGraphPlayer->GetLastKillerEntityId() &&
							   serialPlayer->GetNextBasicAttackTick() == taskGraphPlayer->GetNextBasicAttackTick() &&
							   IsEqual(serialPlayer->GetPosition(), taskGraphPlayer->GetPosition()) &&
							   IsEqual(serialPlayer->GetDirection(), taskGraphPlayer->GetDirection());
			Require(equal,
				std::format("Player state diverged. tick={} entity={} serialSector={} taskGraphSector={} serialSeq={} "
							"taskGraphSeq={} serialPos={} taskGraphPos={}",
					tickIndex,
					entityId,
					serialPlayer->GetSectorId(),
					taskGraphPlayer->GetSectorId(),
					serialPlayer->GetLastMoveSequence(),
					taskGraphPlayer->GetLastMoveSequence(),
					DescribeVector(serialPlayer->GetPosition()),
					DescribeVector(taskGraphPlayer->GetPosition())));

			const std::optional<FSectorId> serialSectorId = m_serialMap->GetSectorGrid().GetEntitySectorId(entityId);
			const std::optional<FSectorId> taskGraphSectorId = m_taskGraphMap->GetSectorGrid().GetEntitySectorId(entityId);
			Require(serialSectorId == taskGraphSectorId && serialSectorId == serialPlayer->GetSectorId() &&
						m_serialMap->GetSectorGrid().ContainsEntity(*serialSectorId, entityId) &&
						m_taskGraphMap->GetSectorGrid().ContainsEntity(*taskGraphSectorId, entityId),
				std::format("Entity/Sector ownership invariant diverged. tick={} entity={}", tickIndex, entityId));
		}

		void FinishSuccessfully()
		{
			Require(m_observedMonsterAiResult, "Monster AI produced no committed result during the equivalence scenario.");
			Require(m_observedMonsterTarget, "Monster AI never acquired a target during the equivalence scenario.");
			Require(m_observedMonsterChase, "Monster AI never entered Chase during the equivalence scenario.");
			Require(m_observedMonsterReturn, "Monster AI never entered Return during the equivalence scenario.");
			Require(m_observedMonsterAttack, "Monster AI never committed an attack during the equivalence scenario.");
			std::lock_guard lock(m_completionLock);
			if (m_finished)
			{
				return;
			}
			m_passed = true;
			m_finished = true;
			m_completionCondition.notify_all();
		}

		void Fail(
			std::string reason)
		{
			std::lock_guard lock(m_completionLock);
			if (m_finished)
			{
				return;
			}
			m_passed = false;
			m_finished = true;
			m_failureReason = std::move(reason);
			m_completionCondition.notify_all();
		}

	private:
		FContentInstanceId m_contentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::shared_ptr<FTaskGraphSectorExecutionService> m_executionService;
		STestConfig m_config;
		std::unique_ptr<FMapInstance> m_serialMap;
		std::unique_ptr<FMapInstance> m_taskGraphMap;
		std::vector<FEntityId> m_entityIds;
		std::unordered_map<FEntityId, FMoveSequence> m_nextMoveSequences;
		std::unordered_map<FEntityId, FAttackSequence> m_nextAttackSequences;
		SMapTickResult m_pendingSerialResult;
		SMapTickResult m_pendingTaskGraphStart;
		mutable std::mutex m_completionLock;
		std::condition_variable m_completionCondition;
		bool m_finished = false;
		bool m_passed = false;
		std::string m_failureReason;
		std::uint32_t m_completedTickCount = 0;
		std::uint64_t m_finalStateHash = 0;
		std::uint64_t m_hashTraceDigest = 0;
		bool m_observedMonsterAiResult = false;
		bool m_observedMonsterTarget = false;
		bool m_observedMonsterChase = false;
		bool m_observedMonsterReturn = false;
		bool m_observedMonsterAttack = false;
	};

	STestReport RunCase(
		const STestConfig& config)
	{
		NetworkLib::Core::FStubServer server(NetworkLib::Core::EBackendKind::Iocp);
		ContentsRuntime::Routing::FContentRuntime runtime;
		ContentsRuntime::Core::SContentRuntimeConfig runtimeConfig{};
		runtimeConfig.workerThreadCount = config.workerCount;
		runtimeConfig.enableOwnershipTransferPolicy = false;
		runtimeConfig.failFastOnRuntimeError = false;
		runtime.SetConfig(runtimeConfig);

		ContentsRuntime::Core::FContentInstanceIdAllocator allocator;
		const FContentInstanceId ownerInstanceId = allocator.Allocate(WorldServer::Contents::kMapContentId);
		if (!ContentsRuntime::Core::IsValidContentInstanceId(ownerInstanceId))
		{
			return {false, "Owner Content InstanceId allocation failed.", config.label};
		}

		std::vector<FContentInstanceId> executorInstanceIds;
		executorInstanceIds.reserve(config.executorCount);
		for (std::uint32_t index = 0; index < config.executorCount; ++index)
		{
			const FContentInstanceId instanceId = allocator.Allocate(WorldServer::Contents::kSectorExecutorContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(instanceId))
			{
				return {false, "Sector Executor Content InstanceId allocation failed.", config.label};
			}
			executorInstanceIds.push_back(instanceId);
		}

		auto executionService = std::make_shared<FTaskGraphSectorExecutionService>(nullptr, executorInstanceIds, config.pumpBatchSize);
		auto owner = std::make_unique<FEquivalenceOwnerContent>(ownerInstanceId, executionService, config);
		FEquivalenceOwnerContent* const ownerPointer = owner.get();
		if (!runtime.RegisterContent(std::move(owner)))
		{
			return {false, "Owner Content registration failed.", config.label};
		}
		for (const FContentInstanceId executorInstanceId : executorInstanceIds)
		{
			if (!runtime.RegisterContent(std::make_unique<WorldServer::Contents::FSectorExecutorContent>(executorInstanceId, 4096)))
			{
				return {false, "Sector Executor Content registration failed.", config.label};
			}
		}

		executionService->BindBridge(runtime);
		runtime.Start(server);
		const bool startEnqueued = runtime.EnqueueCompletionToInstance(ownerInstanceId,
			[ownerPointer]()
			{
				ownerPointer->Start();
			});
		if (!startEnqueued)
		{
			ownerPointer->MarkTimedOut();
		}
		else if (!ownerPointer->WaitForCompletion(std::chrono::seconds(config.timeoutSeconds)))
		{
			ownerPointer->MarkTimedOut();
		}

		executionService->BeginShutdown();
		runtime.Stop();
		STestReport report = ownerPointer->BuildReport();
		report.taskGraphStats = executionService->GetStatsSnapshot();
		executionService->UnbindBridge(runtime);

		if (report.passed)
		{
			const std::uint64_t sectorCount = static_cast<std::uint64_t>(config.worldWidth / config.sectorSize) *
											  static_cast<std::uint64_t>(config.worldHeight / config.sectorSize);
			const std::uint64_t expectedTaskCount = sectorCount * config.tickCount;
			const STaskGraphSectorExecutionStats& stats = report.taskGraphStats;
			if (stats.startedExecutionCount != config.tickCount || stats.completedExecutionCount != config.tickCount ||
				stats.failedExecutionCount != 0 || stats.canceledExecutionCount != 0 || stats.executedTaskCount != expectedTaskCount ||
				stats.activeTaskCount != 0)
			{
				report.passed = false;
				report.failureReason =
					std::format("TaskGraph stats mismatch. started={} completed={} failed={} canceled={} tasks={}/{} active={}",
						stats.startedExecutionCount,
						stats.completedExecutionCount,
						stats.failedExecutionCount,
						stats.canceledExecutionCount,
						stats.executedTaskCount,
						expectedTaskCount,
						stats.activeTaskCount);
			}
			const std::uint32_t workersObserved = std::popcount(stats.workerMask);
			if (report.passed && config.workerCount > 1 && config.executorCount > 1 && workersObserved < 2)
			{
				report.passed = false;
				report.failureReason = std::format("TaskGraph did not execute on multiple Workers. workerMask=0x{:016X}", stats.workerMask);
			}
		}
		return report;
	}

	void PrintReport(
		const STestConfig& config,
		const STestReport& report)
	{
		const std::uint32_t workersObserved = std::popcount(report.taskGraphStats.workerMask);
		std::cout << '[' << (report.passed ? "PASS" : "FAIL") << "] " << report.label << " ticks=" << report.completedTickCount
				  << " workers=" << config.workerCount << " executors=" << config.executorCount << " batch=" << config.pumpBatchSize
				  << " observedWorkers=" << workersObserved << " maxParallel=" << report.taskGraphStats.maxParallelTaskCount
				  << " tasks=" << report.taskGraphStats.executedTaskCount << " finalHash=0x" << std::hex << report.finalStateHash
				  << " trace=0x" << report.hashTraceDigest << std::dec << '\n';
		if (!report.passed)
		{
			std::cerr << "  reason: " << report.failureReason << '\n';
		}
	}

	bool ParseTickCount(
		const int argumentCount,
		char* arguments[],
		std::uint32_t& outTickCount)
	{
		if (argumentCount == 1)
		{
			return true;
		}
		if (argumentCount != 3 || std::string_view(arguments[1]) != "--ticks")
		{
			return false;
		}

		std::uint64_t parsedValue = 0;
		const std::string_view value = arguments[2];
		const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
		if (error != std::errc{} || end != value.data() + value.size() || parsedValue == 0 || parsedValue > 100'000)
		{
			return false;
		}
		outTickCount = static_cast<std::uint32_t>(parsedValue);
		return true;
	}
}

int main(
	const int argumentCount,
	char* arguments[])
{
	std::uint32_t tickCount = 256;
	if (!ParseTickCount(argumentCount, arguments, tickCount))
	{
		std::cerr << "Usage: WorldExecutionEquivalenceSmokeTest [--ticks 1..100000]\n";
		return 2;
	}

	const std::uint32_t hardwareThreadCount = std::max<std::uint32_t>(1u, std::thread::hardware_concurrency());
	const std::uint32_t parallelWorkerCount = std::min<std::uint32_t>(4u, hardwareThreadCount);
	std::vector<STestConfig> cases;
	cases.push_back({"single-worker-batch-1", 1, 1, 1, tickCount});
	if (parallelWorkerCount > 1)
	{
		cases.push_back({"multi-worker-batch-1", parallelWorkerCount, parallelWorkerCount, 1, tickCount});
		cases.push_back({"multi-worker-batch-16", parallelWorkerCount, parallelWorkerCount, 16, tickCount});
	}

	std::cout << "World Serial/TaskGraph execution equivalence integration test\n"
			  << "  comparison : every committed tick hash + movement/AI/combat output + visibility output + entity/sector state\n"
			  << "  world      : 128 x 128, Sector 8 (256 Sectors), 96 Players, 6 Monsters\n"
			  << "  ticks/case : " << tickCount << "\n\n";

	std::vector<STestReport> reports;
	reports.reserve(cases.size());
	bool passed = true;
	for (const STestConfig& config : cases)
	{
		STestReport report = RunCase(config);
		PrintReport(config, report);
		passed = passed && report.passed;
		reports.push_back(std::move(report));
	}

	if (passed && reports.size() > 1)
	{
		const STestReport& reference = reports.front();
		for (std::size_t index = 1; index < reports.size(); ++index)
		{
			if (reports[index].finalStateHash != reference.finalStateHash || reports[index].hashTraceDigest != reference.hashTraceDigest)
			{
				std::cerr << "[FAIL] Cross-policy hash trace diverged. reference=" << reference.label
						  << " candidate=" << reports[index].label << '\n';
				passed = false;
			}
		}
	}

	std::cout << "\n[equivalence-suite] " << (passed ? "PASS" : "FAIL") << " cases=" << reports.size()
			  << " perTickHash=verified crossPolicyTrace=" << (passed ? "verified" : "failed") << '\n';
	return passed ? 0 : 1;
}
