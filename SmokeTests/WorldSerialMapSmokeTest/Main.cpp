#include "WorldSerialMapSmokeTestPch.h"

namespace WorldCore
{
	class FMapInstanceTestAccess final
	{
	public:
		static void InjectNextMonsterSpawnCommitFailure(
			FMapInstance& map) noexcept
		{
			map.InjectNextMonsterSpawnCommitFailureForTesting();
		}
	};
}

namespace
{
	using namespace WorldCore;

	class FTestRunner final
	{
	public:
		void Run(
			const std::string_view name,
			const std::function<void()>& test)
		{
			try
			{
				test();
				++m_passedCount;
				std::cout << "[PASS] " << name << '\n';
			}
			catch (const std::exception& exception)
			{
				++m_failedCount;
				std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
			}
		}

		[[nodiscard]] int Finish() const
		{
			std::cout << "\nWorldSerialMapSmokeTest: passed=" << m_passedCount << " failed=" << m_failedCount << '\n';
			return m_failedCount == 0 ? 0 : 1;
		}

	private:
		std::uint32_t m_passedCount = 0;
		std::uint32_t m_failedCount = 0;
	};

	void Require(
		const bool condition,
		const std::string_view message)
	{
		if (!condition)
		{
			throw std::runtime_error(std::string(message));
		}
	}

	bool NearlyEqual(
		const float lhs,
		const float rhs,
		const float epsilon = 0.0001f)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	SMapDefinition MakeDefinition(
		const std::uint32_t width = 30,
		const std::uint32_t height = 30,
		const std::uint32_t sectorSize = 10)
	{
		SMapDefinition definition{};
		definition.mapDataId = 1;
		definition.worldWidth = width;
		definition.worldHeight = height;
		definition.sectorSize = sectorSize;
		definition.visibilitySectorRadius = 1;
		definition.spawnPosition = {1.0f, 1.0f};
		definition.playerSpawnAreaMinimum = {0.0f, 0.0f};
		definition.playerSpawnAreaMaximum = {static_cast<float>(width), static_cast<float>(height)};
		definition.playerRespawnDelayTicks = 3;
		definition.maxAcceptedPositionError = 16.0f;
		definition.tickRateHz = 20;
		definition.sectorExecutionMode = ESectorExecutionMode::Serial;
		definition.combatPolicy.minimumDamage = 1;
		definition.combatPolicy.playerBasicAttackRange = 64.0f;
		definition.combatPolicy.playerBasicAttackCooldownMilliseconds = 1'000;
		return definition;
	}

	SMonsterRuntimeSnapshot MakeMonsterRuntimeSnapshot(
		const FMonsterDataId monsterDataId = 1'001,
		const EMonsterType monsterType = EMonsterType::Normal,
		const std::uint32_t maxHp = 50,
		const EMonsterAggroType aggroType = EMonsterAggroType::Aggressive)
	{
		SMonsterRuntimeSnapshot snapshot{};
		snapshot.monsterDataId = monsterDataId;
		snapshot.monsterType = monsterType;
		snapshot.aggroType = aggroType;
		snapshot.maxHp = maxHp;
		snapshot.attack = 5;
		snapshot.defense = 1;
		snapshot.moveSpeed = 64.0f;
		snapshot.collisionRadius = 1.0f;
		snapshot.aggroRadius = 8.0f;
		snapshot.leashRadius = 16.0f;
		snapshot.attackRange = 3.0f;
		snapshot.attackCooldownMilliseconds = 1'000;
		snapshot.attackCooldownTicks = 20;
		return snapshot;
	}

	SMonsterSpawnerRuntimeDefinition MakeMonsterSpawner(
		const FMapDataId mapDataId,
		const FSpawnerDataId spawnerDataId = 3'001,
		const FMonsterDataId monsterDataId = 1'001,
		const SVector2 areaMinimum = {10.0f, 10.0f},
		const SVector2 areaMaximum = {40.0f, 40.0f},
		const std::uint32_t initialSpawnCount = 3,
		const std::uint32_t maxAliveCount = 3,
		const std::uint64_t respawnDelayTicks = 2)
	{
		SMonsterSpawnerRuntimeDefinition definition{};
		definition.spawnerDataId = spawnerDataId;
		definition.mapDataId = mapDataId;
		definition.monsterSnapshot = MakeMonsterRuntimeSnapshot(monsterDataId);
		definition.areaMinimum = areaMinimum;
		definition.areaMaximum = areaMaximum;
		definition.initialSpawnCount = initialSpawnCount;
		definition.maxAliveCount = maxAliveCount;
		definition.respawnDelayTicks = respawnDelayTicks;
		return definition;
	}

	SPlayerRuntimeSnapshot MakeCombatPlayerRuntimeSnapshot(
		const FEntityId entityId,
		const std::uint32_t maxHp = 100,
		const std::uint32_t defense = 0,
		const std::uint32_t attack = 1)
	{
		SPlayerRuntimeSnapshot snapshot{};
		snapshot.characterId = entityId + 100'000;
		snapshot.characterDataId = 1;
		snapshot.level = 1;
		snapshot.progressVersion = 1;
		snapshot.statVersion = 1;
		snapshot.maxHp = maxHp;
		snapshot.maxMp = 10;
		snapshot.attack = attack;
		snapshot.defense = defense;
		snapshot.moveSpeedMilli = 1000;
		snapshot.collisionRadius = 1.0f;
		snapshot.equipmentVersion = 1;
		snapshot.statRevision = 1;
		return snapshot;
	}

	std::unique_ptr<FMapInstance> CreateMap(
		const FMapInstanceId mapInstanceId,
		const SMapDefinition& definition)
	{
		FMapInstanceFactory factory;
		EMapCreateResult result = EMapCreateResult::InvalidDefinition;
		std::string error;
		std::unique_ptr<FMapInstance> map = factory.Create(mapInstanceId, definition, result, error);
		Require(map != nullptr, error);
		Require(result == EMapCreateResult::Success, "Map factory did not report Success.");
		return map;
	}

	void AddPlayer(
		FMapInstance& map,
		const FEntityId entityId,
		const SVector2& position,
		std::vector<SVisibilityEvent>* const outEvents = nullptr)
	{
		std::vector<SVisibilityEvent> events;
		std::string error;
		Require(
			map.AddPlayer(
				entityId, entityId + 10'000, position, {1.0f, 0.0f}, MakeCombatPlayerRuntimeSnapshot(entityId, 1'000'000), events, error),
			error);
		if (outEvents != nullptr)
		{
			*outEvents = std::move(events);
		}
	}

	void AddCombatPlayer(
		FMapInstance& map,
		const FEntityId entityId,
		const SVector2& position,
		const std::uint32_t maxHp,
		const std::uint32_t defense = 0,
		const std::uint32_t attack = 1)
	{
		std::vector<SVisibilityEvent> events;
		std::string error;
		Require(map.AddPlayer(entityId,
					entityId + 10'000,
					position,
					{1.0f, 0.0f},
					MakeCombatPlayerRuntimeSnapshot(entityId, maxHp, defense, attack),
					events,
					error),
			error.empty() ? "AddCombatPlayer failed without a diagnostic." : error);
	}

	bool HasVisibilityEvent(
		const std::span<const SVisibilityEvent> events,
		const EVisibilityEventKind kind,
		const FEntityId observerEntityId,
		const FEntityId subjectEntityId)
	{
		return std::any_of(events.begin(),
			events.end(),
			[&](const SVisibilityEvent& event)
			{
				return event.kind == kind && event.observerEntityId == observerEntityId && event.subjectEntityId == subjectEntityId;
			});
	}

	const SMonsterAiResult* FindMonsterAiResult(
		const SMapTickResult& tick,
		const FEntityId entityId)
	{
		const auto iterator = std::ranges::find_if(tick.monsterAiResults,
			[entityId](const SMonsterAiResult& result)
			{
				return result.entityId == entityId;
			});
		return iterator == tick.monsterAiResults.end() ? nullptr : &*iterator;
	}

	class FPendingSectorExecutor final : public ISectorExecutor
	{
	public:
		[[nodiscard]] SSectorExecutionStartResult Execute(
			const SMapTickTicket& ticket,
			const FSectorTickPlan& tickPlan,
			const FSectorTaskProcessor& taskProcessor,
			const FEntityRegistry& entityRegistry,
			const FSectorGrid& sectorGrid,
			const SMapDefinition& mapDefinition) override
		{
			++m_executeCount;
			m_ticket = ticket;
			m_preparedOutputs.clear();
			m_capturedMoveRequests.clear();

			SSectorExecutionStartResult result{};
			for (const SSectorTaskWave& wave : tickPlan.GetWaves())
			{
				for (const SSectorTask& task : wave.tasks)
				{
					for (const SMoveCommand& command : task.moveCommands)
					{
						m_capturedMoveRequests.push_back({command.entityId, command.sequence});
					}

					SSectorTaskOutput output{};
					if (!taskProcessor.Execute(task, entityRegistry, sectorGrid, mapDefinition, output, result.failureReason))
					{
						return result;
					}
					m_preparedOutputs.push_back(std::move(output));
				}
			}

			std::sort(m_preparedOutputs.begin(),
				m_preparedOutputs.end(),
				[](const SSectorTaskOutput& lhs, const SSectorTaskOutput& rhs)
				{
					return lhs.stableOrder < rhs.stableOrder;
				});
			result.executionResult = ESectorExecutionResult::Pending;
			return result;
		}

		[[nodiscard]] SMapTickExecutionCompletion MakeSuccessCompletion() const
		{
			SMapTickExecutionCompletion completion{};
			completion.ticket = m_ticket;
			completion.status = EMapTickCompletionStatus::Succeeded;
			completion.taskOutputs = m_preparedOutputs;
			return completion;
		}

		[[nodiscard]] SMapTickExecutionCompletion MakeFailureCompletion(
			const std::string_view failureReason) const
		{
			SMapTickExecutionCompletion completion{};
			completion.ticket = m_ticket;
			completion.status = EMapTickCompletionStatus::Failed;
			completion.failureReason = failureReason;
			return completion;
		}

		[[nodiscard]] std::size_t GetExecuteCount() const noexcept
		{
			return m_executeCount;
		}

		[[nodiscard]] const SMapTickTicket& GetTicket() const noexcept
		{
			return m_ticket;
		}

		[[nodiscard]] std::span<const SMoveRequestIdentity> GetCapturedMoveRequests() const noexcept
		{
			return m_capturedMoveRequests;
		}

	private:
		std::size_t m_executeCount = 0;
		SMapTickTicket m_ticket{};
		std::vector<SSectorTaskOutput> m_preparedOutputs;
		std::vector<SMoveRequestIdentity> m_capturedMoveRequests;
	};

	std::unique_ptr<FMapInstance> CreatePendingMap(
		const FMapInstanceId mapInstanceId,
		SMapDefinition definition,
		FPendingSectorExecutor*& outExecutor)
	{
		definition.sectorExecutionMode = ESectorExecutionMode::TaskGraph;
		auto executor = std::make_unique<FPendingSectorExecutor>();
		outExecutor = executor.get();
		FMapInstanceFactory factory;
		EMapCreateResult result = EMapCreateResult::InvalidDefinition;
		std::string error;
		std::unique_ptr<FMapInstance> map = factory.CreateWithExecutor(mapInstanceId, definition, std::move(executor), result, error);
		Require(map != nullptr && result == EMapCreateResult::Success, error);
		return map;
	}

	void TestMapDefinitionAndSectorCoordinates()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 10);
		std::string error;
		Require(IsValidMapDefinition(definition, error), error);

		FSectorGrid grid(definition);
		Require(grid.GetColumnCount() == 10 && grid.GetRowCount() == 10 && grid.GetSectorCount() == 100,
			"Sector grid dimensions are incorrect.");

		FSectorId sectorId = kInvalidSectorId;
		Require(grid.TryResolveSector({0.0f, 0.0f}, sectorId) && sectorId == 0, "World origin mapped incorrectly.");
		Require(grid.TryResolveSector({9.999f, 9.999f}, sectorId) && sectorId == 0, "Sector upper edge mapped incorrectly.");
		Require(grid.TryResolveSector({10.0f, 0.0f}, sectorId) && sectorId == 1, "Exact Sector boundary mapped incorrectly.");
		Require(grid.TryResolveSector({99.999f, 99.999f}, sectorId) && sectorId == 99, "World upper edge mapped incorrectly.");
		Require(!grid.TryResolveSector({-0.001f, 0.0f}, sectorId), "Negative coordinate was accepted.");
		Require(!grid.TryResolveSector({100.0f, 0.0f}, sectorId), "Exclusive World boundary was accepted.");

		const SVector2 clamped = grid.ClampInsideWorld({100.0f, -2.0f});
		Require(clamped.x < 100.0f && clamped.x > 99.0f && clamped.y == 0.0f, "World clamp is incorrect.");
		Require(grid.GetNearbySectorIds(0, 1).size() == 4, "Corner visibility Sector count is incorrect.");
		Require(grid.GetNearbySectorIds(55, 1).size() == 9, "Center visibility Sector count is incorrect.");

		definition.worldWidth = 101;
		Require(!IsValidMapDefinition(definition, error), "Non-divisible World dimensions were accepted.");
	}

	std::vector<SSectorTask> MakeSectorTasks(
		const FSectorGrid& grid,
		const std::uint64_t tickIndex)
	{
		std::vector<SSectorTask> tasks;
		tasks.reserve(grid.GetSectorCount());
		for (FSectorId nextSectorId = grid.GetSectorCount(); nextSectorId > 0; --nextSectorId)
		{
			const FSectorId sectorId = nextSectorId - 1;
			tasks.push_back({sectorId, sectorId, tickIndex, {}});
		}
		return tasks;
	}

	void VerifySectorTickPlan(
		const SMapDefinition& definition)
	{
		FSectorGrid grid(definition);
		FSectorTickPlan plan;
		std::string error;
		Require(plan.Build(grid, MakeSectorTasks(grid, 77), error), error);
		Require(plan.GetTickIndex() == 77, "Sector Tick Plan lost its Tick index.");
		Require(plan.GetTaskCount() == grid.GetSectorCount(), "Sector Tick Plan task count is incorrect.");
		Require(plan.GetWaves().size() == kSectorTaskWaveCount, "Sector Tick Plan did not create four Waves.");

		std::vector<std::uint8_t> seenSectors(grid.GetSectorCount(), 0);
		std::vector<std::uint32_t> waveBySector(grid.GetSectorCount(), std::numeric_limits<std::uint32_t>::max());
		std::size_t taskCount = 0;
		for (std::uint32_t waveIndex = 0; waveIndex < plan.GetWaves().size(); ++waveIndex)
		{
			const SSectorTaskWave& wave = plan.GetWaves()[waveIndex];
			Require(wave.waveIndex == waveIndex, "Sector Tick Plan Wave index is unstable.");
			FSectorId previousStableOrder = 0;
			bool hasPreviousTask = false;
			for (const SSectorTask& task : wave.tasks)
			{
				Require(task.sectorId < grid.GetSectorCount(), "Sector Tick Plan contains an invalid Sector.");
				Require(seenSectors[task.sectorId] == 0, "Sector Tick Plan contains a duplicate Sector.");
				Require(task.stableOrder == task.sectorId && task.tickIndex == 77, "Sector Tick Plan changed the Task identity.");
				if (hasPreviousTask)
				{
					Require(previousStableOrder < task.stableOrder, "Wave Tasks are not in stable row-major order.");
				}

				const std::uint32_t x = task.sectorId % grid.GetColumnCount();
				const std::uint32_t y = task.sectorId / grid.GetColumnCount();
				const std::uint32_t expectedWaveIndex = (x & 1u) | ((y & 1u) << 1u);
				Require(expectedWaveIndex == waveIndex, "Sector Task was assigned to the wrong Wave.");
				seenSectors[task.sectorId] = 1;
				waveBySector[task.sectorId] = waveIndex;
				previousStableOrder = task.stableOrder;
				hasPreviousTask = true;
				++taskCount;
			}
		}

		Require(taskCount == grid.GetSectorCount(), "Sector Tick Plan omitted a Sector Task.");
		Require(std::all_of(seenSectors.begin(),
					seenSectors.end(),
					[](const std::uint8_t seen)
					{
						return seen == 1;
					}),
			"Sector Tick Plan did not cover every Sector exactly once.");

		for (std::uint32_t y = 0; y < grid.GetRowCount(); ++y)
		{
			for (std::uint32_t x = 0; x < grid.GetColumnCount(); ++x)
			{
				const FSectorId sectorId = y * grid.GetColumnCount() + x;
				for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY)
				{
					for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX)
					{
						if (offsetX == 0 && offsetY == 0)
						{
							continue;
						}
						const std::int32_t neighborX = static_cast<std::int32_t>(x) + offsetX;
						const std::int32_t neighborY = static_cast<std::int32_t>(y) + offsetY;
						if (neighborX < 0 || neighborY < 0 || neighborX >= static_cast<std::int32_t>(grid.GetColumnCount()) ||
							neighborY >= static_cast<std::int32_t>(grid.GetRowCount()))
						{
							continue;
						}

						const FSectorId neighborId =
							static_cast<FSectorId>(neighborY) * grid.GetColumnCount() + static_cast<FSectorId>(neighborX);
						Require(waveBySector[sectorId] != waveBySector[neighborId], "Adjacent Sectors were assigned to the same Wave.");
					}
				}
			}
		}
	}

	void TestSectorTickPlanWavePartition()
	{
		VerifySectorTickPlan(MakeDefinition(10, 10, 10));
		VerifySectorTickPlan(MakeDefinition(10, 50, 10));
		VerifySectorTickPlan(MakeDefinition(50, 10, 10));
		VerifySectorTickPlan(MakeDefinition(20, 20, 10));
		VerifySectorTickPlan(MakeDefinition(30, 30, 10));
		VerifySectorTickPlan(MakeDefinition(50, 30, 10));

		const SMapDefinition definition = MakeDefinition(30, 30, 10);
		FSectorGrid grid(definition);
		FSectorTickPlan plan;
		std::string error;
		std::vector<SSectorTask> tasks = MakeSectorTasks(grid, 88);
		tasks.pop_back();
		Require(!plan.Build(grid, tasks, error) && plan.GetTaskCount() == 0, "Sector Tick Plan accepted a missing Sector Task.");

		tasks = MakeSectorTasks(grid, 88);
		tasks.back().sectorId = tasks.front().sectorId;
		tasks.back().stableOrder = tasks.front().stableOrder;
		Require(!plan.Build(grid, tasks, error) && plan.GetTaskCount() == 0, "Sector Tick Plan accepted a duplicate Sector Task.");

		tasks = MakeSectorTasks(grid, 88);
		tasks.front().stableOrder = 0;
		Require(!plan.Build(grid, tasks, error) && plan.GetTaskCount() == 0, "Sector Tick Plan accepted a non-canonical stable order.");

		tasks = MakeSectorTasks(grid, 88);
		tasks.front().tickIndex = 89;
		Require(!plan.Build(grid, tasks, error) && plan.GetTaskCount() == 0, "Sector Tick Plan accepted Tasks from different Ticks.");
	}

	void TestEntityRegistry()
	{
		FEntityRegistry registry;
		Require(registry.AddPlayer(2, 102, {2.0f, 2.0f}, {2.0f, 0.0f}, 0), "Player registration failed.");
		Require(registry.AddPlayer(1, 101, {1.0f, 1.0f}, {0.0f, 1.0f}, 0), "Second Player registration failed.");
		Require(!registry.AddPlayer(1, 999, {3.0f, 3.0f}, {}, 0), "Duplicate EntityId was accepted.");
		Require(registry.GetPlayerCount() == 2, "Registry player count is incorrect.");
		Require(registry.GetPlayerEntityIds() == std::vector<FEntityId>({1, 2}), "Registry IDs are not stable-sorted.");
		const FPlayerEntity* const player = registry.FindPlayer(2);
		Require(player != nullptr && player->GetUserId() == 102, "Registered Player lookup failed.");
		Require(NearlyEqual(player->GetDirection().x, 1.0f), "Player direction was not normalized.");
		Require(registry.RemovePlayer(2) && !registry.RemovePlayer(2), "Player removal result is incorrect.");
	}

	void TestActorAndMonsterRegistry()
	{
		FEntityRegistry registry;
		const SMonsterRuntimeSnapshot monsterSnapshot = MakeMonsterRuntimeSnapshot();
		Require(registry.AddPlayer(20, 120, {2.0f, 3.0f}, {1.0f, 0.0f}, 0), "Actor registry Player registration failed.");
		Require(registry.AddMonster(10, 3'001, 1, {4.0f, 5.0f}, {0.0f, 2.0f}, 1, monsterSnapshot, monsterSnapshot.maxHp),
			"Actor registry Monster registration failed.");
		Require(!registry.AddMonster(20, 3'002, 1, {6.0f, 7.0f}, {1.0f, 0.0f}, 1, monsterSnapshot, monsterSnapshot.maxHp),
			"A Monster reused an existing Player EntityId.");
		Require(!registry.AddPlayer(10, 999, {8.0f, 9.0f}, {1.0f, 0.0f}, 1), "A Player reused an existing Monster EntityId.");
		SMonsterRuntimeSnapshot invalidMonsterSnapshot = monsterSnapshot;
		invalidMonsterSnapshot.monsterDataId = kInvalidMonsterDataId;
		Require(!registry.AddMonster(30, 3'003, 1, {1.0f, 1.0f}, {}, 0, invalidMonsterSnapshot, monsterSnapshot.maxHp) &&
					!registry.AddMonster(30, kInvalidSpawnerDataId, 1, {1.0f, 1.0f}, {}, 0, monsterSnapshot, monsterSnapshot.maxHp) &&
					!registry.AddMonster(30, 3'003, kInvalidSpawnGeneration, {1.0f, 1.0f}, {}, 0, monsterSnapshot, monsterSnapshot.maxHp) &&
					!registry.AddMonster(30, 3'003, 1, {1.0f, 1.0f}, {}, 0, monsterSnapshot, 0) &&
					!registry.AddMonster(30, 3'003, 1, {1.0f, 1.0f}, {}, 0, monsterSnapshot, monsterSnapshot.maxHp + 1),
			"Monster registration accepted an invalid identity or HP value.");

		Require(registry.GetActorCount() == 2 && registry.GetPlayerCount() == 1 && registry.GetMonsterCount() == 1,
			"Actor registry type counts are incorrect.");
		Require(registry.GetActorEntityIds() == std::vector<FEntityId>({10, 20}) &&
					registry.GetPlayerEntityIds() == std::vector<FEntityId>({20}) &&
					registry.GetMonsterEntityIds() == std::vector<FEntityId>({10}),
			"Actor registry type-specific IDs are incorrect.");

		const FActorEntity* const playerActor = registry.FindActor(20);
		const FActorEntity* const monsterActor = registry.FindActor(10);
		const FMonsterEntity* const monster = registry.FindMonster(10);
		Require(playerActor != nullptr && playerActor->GetActorKind() == EActorKind::Player, "Player Actor kind lookup failed.");
		Require(monsterActor != nullptr && monsterActor->GetActorKind() == EActorKind::Monster, "Monster Actor kind lookup failed.");
		Require(monster != nullptr && monster->GetMonsterDataId() == 1'001 && monster->GetSpawnerDataId() == 3'001 &&
					monster->GetSpawnGeneration() == 1 && monster->GetSectorId() == 1 && NearlyEqual(monster->GetDirection().y, 1.0f) &&
					monster->GetSpawnPosition() == SVector2{4.0f, 5.0f} && monster->GetRuntimeSnapshot() == monsterSnapshot &&
					monster->GetCurrentHp() == monsterSnapshot.maxHp,
			"Registered Monster state is incorrect.");
		Require(registry.FindPlayer(10) == nullptr && registry.FindMonster(20) == nullptr,
			"A typed Actor lookup returned the wrong derived type.");

		Require(!registry.RemovePlayer(10) && registry.Contains(10), "Player removal erased a Monster.");
		Require(!registry.RemoveMonster(20) && registry.Contains(20), "Monster removal erased a Player.");
		Require(registry.RemoveMonster(10) && !registry.RemoveMonster(10), "Monster removal result is incorrect.");
		Require(registry.RemoveActor(20) && registry.GetActorCount() == 0 && registry.GetPlayerCount() == 0,
			"Generic Actor removal did not update Registry ownership.");
	}

	void TestMonsterVisibilitySubject()
	{
		const SMapDefinition definition = MakeDefinition(30, 10, 10);
		FEntityRegistry registry;
		FSectorGrid grid(definition);
		FMapVisibilitySystem visibility;
		const SMonsterRuntimeSnapshot monsterSnapshot = MakeMonsterRuntimeSnapshot();

		Require(registry.AddPlayer(1, 101, {1.0f, 1.0f}, {1.0f, 0.0f}, 0) && grid.AddEntity(0, 1), "Visibility observer setup failed.");
		Require(registry.AddPlayer(2, 102, {21.0f, 1.0f}, {-1.0f, 0.0f}, 2) && grid.AddEntity(2, 2),
			"Attacker-only visibility observer setup failed.");
		Require(registry.AddMonster(10, 3'001, 1, {11.0f, 1.0f}, {0.0f, 1.0f}, 1, monsterSnapshot, monsterSnapshot.maxHp) &&
					grid.AddEntity(1, 10),
			"Visibility Monster setup failed.");

		const std::vector<SVisibilityEvent> events = visibility.Refresh(registry, grid, 1, {});
		Require(events.size() == 2 && HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 1, 10) &&
					HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 2, 10),
			"A nearby Monster was not exposed as a Player visibility subject.");
		Require(events[0].moveSequence == 0 && events[0].position == SVector2{11.0f, 1.0f} && events[0].direction == SVector2{0.0f, 1.0f},
			"Monster visibility payload is incorrect.");
		const FActorEntity* const spawnActor = registry.FindActor(events[0].subjectEntityId);
		const FMonsterEntity* const spawnMonster = registry.FindMonster(events[0].subjectEntityId);
		Require(spawnActor != nullptr && spawnActor->GetActorKind() == EActorKind::Monster && spawnMonster != nullptr &&
					spawnMonster->GetMonsterDataId() == monsterSnapshot.monsterDataId,
			"Monster Spawn subject could not provide the common ActorKind/ActorDataId packet identity.");
		Require(
			!HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 10, 1), "A Monster was incorrectly treated as a visibility observer.");

		const std::vector<SActorAttackResult> attacks{{10, 1, 3, 97, 100}};
		const std::vector<SActorAttackEvent> attackEvents = visibility.BuildActorAttackEvents(registry, attacks);
		Require(attackEvents.size() == 1 && attackEvents.front().observerEntityId == 1,
			"Combined attack/damage notification was sent to an observer that sees only the attacker.");
	}

	SPlayerRuntimeSnapshot MakePlayerRuntimeSnapshot(
		const std::uint64_t statRevision,
		const std::uint32_t maxHp,
		const std::uint32_t maxMp)
	{
		SPlayerRuntimeSnapshot snapshot{};
		snapshot.characterId = 7001;
		snapshot.characterDataId = 1;
		snapshot.level = 10;
		snapshot.exp = 450;
		snapshot.requiredExpToNextLevel = 1'000;
		snapshot.str = 11;
		snapshot.dex = 12;
		snapshot.intelligence = 13;
		snapshot.luk = 14;
		snapshot.unspentStatPoints = 5;
		snapshot.progressVersion = 2;
		snapshot.statVersion = statRevision;
		snapshot.finalStr = 21;
		snapshot.finalDex = 22;
		snapshot.finalIntelligence = 23;
		snapshot.finalLuk = 24;
		snapshot.maxHp = maxHp;
		snapshot.maxMp = maxMp;
		snapshot.attack = 33;
		snapshot.defense = 17;
		snapshot.moveSpeedMilli = 4'000;
		snapshot.collisionRadius = 1.0f;
		snapshot.equipmentVersion = 3;
		snapshot.statRevision = statRevision;
		return snapshot;
	}

	void TestPlayerRuntimeSnapshotReplacement()
	{
		FEntityRegistry registry;
		const SPlayerRuntimeSnapshot initialSnapshot = MakePlayerRuntimeSnapshot(10, 100, 50);
		Require(registry.AddPlayer(1, 101, {1.0f, 1.0f}, {0.0f, 1.0f}, 0, initialSnapshot),
			"Player registration with a runtime Snapshot failed.");

		FPlayerEntity* const player = registry.FindPlayer(1);
		Require(player != nullptr && player->HasRuntimeSnapshot(), "Registered Player lost its runtime Snapshot.");
		Require(player->GetRuntimeSnapshot() == initialSnapshot, "Initial runtime Snapshot was changed.");
		Require(player->GetCurrentHp() == 100 && player->GetCurrentMp() == 50,
			"Initial runtime Snapshot did not initialize HP and MP to their maxima.");

		SPlayerRuntimeSnapshot sameRevision = MakePlayerRuntimeSnapshot(10, 80, 40);
		Require(!player->ApplyRuntimeSnapshot(sameRevision), "A duplicate StatRevision replaced the runtime Snapshot.");
		Require(player->GetCurrentHp() == 100 && player->GetCurrentMp() == 50, "Rejected duplicate Snapshot changed HP or MP.");

		SPlayerRuntimeSnapshot staleRevision = MakePlayerRuntimeSnapshot(9, 70, 30);
		Require(!player->ApplyRuntimeSnapshot(staleRevision), "A stale StatRevision replaced the runtime Snapshot.");

		SPlayerRuntimeSnapshot lowerMaxima = MakePlayerRuntimeSnapshot(11, 80, 40);
		Require(player->ApplyRuntimeSnapshot(lowerMaxima), "A newer runtime Snapshot was rejected.");
		Require(player->GetRuntimeSnapshot() == lowerMaxima, "Newer runtime Snapshot was not stored.");
		Require(player->GetCurrentHp() == 80 && player->GetCurrentMp() == 40, "HP and MP were not clamped to the newer maxima.");

		SPlayerRuntimeSnapshot higherMaxima = MakePlayerRuntimeSnapshot(12, 120, 60);
		Require(player->ApplyRuntimeSnapshot(higherMaxima), "A second newer runtime Snapshot was rejected.");
		Require(player->GetCurrentHp() == 80 && player->GetCurrentMp() == 40,
			"A projection refresh healed HP or MP instead of preserving the current values.");

		SPlayerRuntimeSnapshot differentCharacter = MakePlayerRuntimeSnapshot(13, 120, 60);
		differentCharacter.characterId = 7002;
		Require(!player->ApplyRuntimeSnapshot(differentCharacter), "A Snapshot for another Character replaced the Player state.");
	}

	void TestInputSequenceAndTickIsolation()
	{
		FMapInputBuffer inputBuffer;
		Require(inputBuffer.EnqueueMove({1, 2, EMoveState::Start, {2.0f, 1.0f}, {1.0f, 0.0f}}), "Initial move was rejected.");
		Require(!inputBuffer.EnqueueMove({1, 1, EMoveState::Sync, {1.0f, 1.0f}, {1.0f, 0.0f}}), "Older sequence was accepted.");
		Require(inputBuffer.EnqueueMove({1, 3, EMoveState::Sync, {3.0f, 1.0f}, {1.0f, 0.0f}}), "Newer sequence was rejected.");
		Require(inputBuffer.GetPendingMoveCount() == 1, "Latest input did not replace the previous pending input.");

		const std::vector<SMoveCommand> firstTick = inputBuffer.BeginTick();
		Require(firstTick.size() == 1 && firstTick[0].sequence == 3, "Tick did not capture the latest sequence.");
		Require(inputBuffer.EnqueueMove({1, 4, EMoveState::Stop, {4.0f, 1.0f}, {0.0f, 0.0f}}), "Next-Tick input was rejected.");
		Require(firstTick[0].sequence == 3, "New input modified the active Tick snapshot.");
		const std::vector<SMoveCommand> secondTick = inputBuffer.BeginTick();
		Require(secondTick.size() == 1 && secondTick[0].sequence == 4, "Next-Tick input was not isolated.");

		Require(inputBuffer.EnqueueMove({1, 5, EMoveState::Start, {5.0f, 1.0f}, {1.0f, 0.0f}}), "Pending move for discard was rejected.");
		const std::optional<SMoveRequestIdentity> discarded = inputBuffer.DiscardPendingMove(1);
		Require(discarded.has_value() && *discarded == SMoveRequestIdentity{1, 5} && inputBuffer.GetPendingMoveCount() == 0,
			"DiscardPendingMove did not return and remove the exact queued request.");
		Require(!inputBuffer.EnqueueMove({1, 5, EMoveState::Stop, {5.0f, 1.0f}, {0.0f, 0.0f}}),
			"DiscardPendingMove erased the latest sequence guard.");
		Require(inputBuffer.EnqueueMove({1, 6, EMoveState::Stop, {6.0f, 1.0f}, {0.0f, 0.0f}}),
			"A newer move was rejected after discarding only the pending request.");
	}

	void TestSerialTaskOrderAndReadOnlyExecution()
	{
		const SMapDefinition definition = MakeDefinition(30, 10, 10);
		FEntityRegistry registry;
		FSectorGrid grid(definition);
		for (FEntityId entityId = 1; entityId <= 3; ++entityId)
		{
			const FSectorId sectorId = static_cast<FSectorId>(entityId - 1);
			const SVector2 position{static_cast<float>(sectorId * 10 + 1), 1.0f};
			Require(
				registry.AddPlayer(entityId, entityId + 100, position, {1.0f, 0.0f}, sectorId), "Task test Player registration failed.");
			Require(grid.AddEntity(sectorId, entityId), "Task test Sector registration failed.");
		}

		std::vector<SSectorTask> tasks(3);
		tasks[0] = {2, 2, 1, {{3, 1, EMoveState::Sync, {22.0f, 1.0f}, {1.0f, 0.0f}}}};
		tasks[1] = {0, 0, 1, {{1, 1, EMoveState::Sync, {2.0f, 1.0f}, {1.0f, 0.0f}}}};
		tasks[2] = {1, 1, 1, {{2, 1, EMoveState::Sync, {12.0f, 1.0f}, {1.0f, 0.0f}}}};

		FSerialSectorExecutor executor;
		FSectorTaskProcessor processor;
		FSectorTickPlan tickPlan;
		std::string error;
		Require(tickPlan.Build(grid, tasks, error), error);
		const SMapTickTicket ticket{1, 1, 1, 1};
		SSectorExecutionStartResult execution = executor.Execute(ticket, tickPlan, processor, registry, grid, definition);
		Require(execution.executionResult == ESectorExecutionResult::CompletedInline, execution.failureReason);
		const std::vector<SSectorTaskOutput>& outputs = execution.taskOutputs;
		Require(outputs.size() == 3 && outputs[0].stableOrder == 0 && outputs[1].stableOrder == 1 && outputs[2].stableOrder == 2,
			"Serial tasks did not execute in stable order.");
		Require(registry.FindPlayer(1)->GetPosition() == SVector2{1.0f, 1.0f}, "Sector task mutated an Entity before Commit.");
		Require(grid.GetEntitySectorId(1) == std::optional<FSectorId>(0), "Sector task mutated the Grid before Commit.");

		tasks[0].stableOrder = 0;
		Require(!tickPlan.Build(grid, tasks, error), "Duplicate stable order was not rejected by the Tick Plan.");
		Require(executor.Execute(ticket, tickPlan, processor, registry, grid, definition).executionResult == ESectorExecutionResult::Failed,
			"Serial execution accepted a cleared invalid Tick Plan.");
	}

	void TestMovementCorrectionAndDeferredTransfer()
	{
		SMapDefinition definition = MakeDefinition(30, 10, 10);
		definition.maxAcceptedPositionError = 5.0f;
		std::unique_ptr<FMapInstance> map = CreateMap(100, definition);
		AddPlayer(*map, 1, {9.0f, 1.0f});

		Require(map->QueueMove({1, 1, EMoveState::Start, {11.0f, 1.0f}, {2.0f, 0.0f}}), "Cross-Sector move was rejected.");
		Require(map->FindPlayer(1)->GetPosition() == SVector2{9.0f, 1.0f}, "Queued movement changed Entity state before Tick.");
		Require(
			map->GetSectorGrid().GetEntitySectorId(1) == std::optional<FSectorId>(0), "Queued movement changed Sector state before Tick.");

		const SMapTickResult transferTick = map->Tick();
		Require(transferTick.result == EMapTickResult::Completed, transferTick.failureReason);
		Require(
			transferTick.moveResults.size() == 1 && !transferTick.moveResults[0].isCorrected, "Valid movement was unexpectedly corrected.");
		Require(map->FindPlayer(1)->GetPosition() == SVector2{11.0f, 1.0f}, "Movement position was not committed.");
		Require(map->GetSectorGrid().GetEntitySectorId(1) == std::optional<FSectorId>(1),
			"Deferred Sector transfer was not committed exactly once.");

		Require(!map->QueueMove({1, 1, EMoveState::Sync, {12.0f, 1.0f}, {1.0f, 0.0f}}), "Duplicate move sequence was accepted.");
		Require(map->QueueMove({1, 2, EMoveState::Sync, {25.0f, 1.0f}, {1.0f, 0.0f}}), "Correction test move was rejected.");
		const SMapTickResult correctionTick = map->Tick();
		Require(correctionTick.moveResults.size() == 1 && correctionTick.moveResults[0].isCorrected,
			"Large client position error was not corrected.");
		Require(map->FindPlayer(1)->GetPosition() == SVector2{11.0f, 1.0f}, "Corrected movement trusted the distant client position.");

		std::unique_ptr<FMapInstance> boundaryMap = CreateMap(101, definition);
		AddPlayer(*boundaryMap, 2, {28.0f, 1.0f});
		Require(boundaryMap->QueueMove({2, 1, EMoveState::Sync, {31.0f, 1.0f}, {1.0f, 0.0f}}), "Boundary test move was rejected.");
		const SMapTickResult boundaryTick = boundaryMap->Tick();
		Require(boundaryTick.moveResults[0].isCorrected && boundaryMap->FindPlayer(2)->GetPosition().x < 30.0f,
			"World boundary movement was not clamped.");
		Require(boundaryMap->GetSectorGrid().GetEntitySectorId(2) == std::optional<FSectorId>(2),
			"Boundary clamp produced an invalid Sector transfer.");
	}

	void TestVisibilityEvents()
	{
		SMapDefinition definition = MakeDefinition(30, 10, 10);
		definition.maxAcceptedPositionError = 20.0f;
		std::unique_ptr<FMapInstance> map = CreateMap(200, definition);
		std::vector<SVisibilityEvent> events;
		AddPlayer(*map, 1, {1.0f, 1.0f}, &events);
		Require(events.empty(), "A lone Player produced a visibility event.");
		AddPlayer(*map, 2, {11.0f, 1.0f}, &events);
		Require(events.size() == 2 && HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 1, 2) &&
					HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 2, 1),
			"Adjacent Players did not receive reciprocal Spawn events.");
		AddPlayer(*map, 3, {21.0f, 1.0f}, &events);
		Require(HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 2, 3) &&
					HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 3, 2) &&
					!HasVisibilityEvent(events, EVisibilityEventKind::Spawn, 1, 3),
			"Visibility radius was not applied by Sector distance.");

		Require(map->QueueMove({2, 1, EMoveState::Sync, {21.0f, 1.0f}, {1.0f, 0.0f}}), "Visibility movement was rejected.");
		const SMapTickResult tick = map->Tick();
		Require(HasVisibilityEvent(tick.visibilityEvents, EVisibilityEventKind::Despawn, 1, 2),
			"Observer did not receive Despawn after Sector range exit.");
		Require(HasVisibilityEvent(tick.visibilityEvents, EVisibilityEventKind::Despawn, 2, 1),
			"Moving observer did not drop an out-of-range subject.");
		Require(HasVisibilityEvent(tick.visibilityEvents, EVisibilityEventKind::Move, 3, 2),
			"Visible observer did not receive Move for the moved subject.");

		std::string error;
		Require(map->RemovePlayer(3, events, error), error);
		Require(HasVisibilityEvent(events, EVisibilityEventKind::Despawn, 2, 3),
			"Remaining observer did not receive Despawn for a removed Player.");
	}

	void TestMultipleMapIsolation()
	{
		FMapInstanceManager manager;
		const SMapDefinition definition = MakeDefinition(30, 10, 10);
		EMapCreateResult createResult = EMapCreateResult::InvalidDefinition;
		std::string error;
		FMapInstance* const first = manager.CreateMap(1, definition, createResult, error);
		Require(first != nullptr, error);
		FMapInstance* const second = manager.CreateMap(2, definition, createResult, error);
		Require(second != nullptr, error);
		Require(manager.CreateMap(1, definition, createResult, error) == nullptr && createResult == EMapCreateResult::DuplicateMapInstance,
			"Duplicate MapInstanceId was accepted.");

		AddPlayer(*first, 7, {1.0f, 1.0f});
		AddPlayer(*second, 7, {21.0f, 1.0f});
		Require(first->QueueMove({7, 1, EMoveState::Sync, {5.0f, 1.0f}, {1.0f, 0.0f}}), "First Map move was rejected.");
		const std::vector<SMapTickResult> results = manager.TickAll();
		Require(results.size() == 2 && results[0].mapInstanceId == 1 && results[1].mapInstanceId == 2,
			"Manager Tick order is not stable by MapInstanceId.");
		Require(first->FindPlayer(7)->GetPosition() == SVector2{5.0f, 1.0f}, "First Map did not advance.");
		Require(second->FindPlayer(7)->GetPosition() == SVector2{21.0f, 1.0f}, "A movement leaked into a different MapInstance.");
		Require(manager.RemoveMap(1) && manager.FindMap(1) == nullptr && manager.GetMapCount() == 1, "Map removal failed.");
	}

	void VerifyCommittedMonsterSpawn(
		const FMapInstance& map,
		const SMonsterSpawnerRuntimeDefinition& spawner,
		const SMonsterSpawnResult& spawn)
	{
		Require((spawn.entityId & kMapLocalEntityIdBit) != 0, "A spawned Monster did not receive a Map-local high-bit EntityId.");
		Require(spawn.monsterDataId == spawner.monsterSnapshot.monsterDataId && spawn.spawnerDataId == spawner.spawnerDataId,
			"Monster spawn result lost its GameData identity.");
		Require(spawn.spawnGeneration != kInvalidSpawnGeneration, "Monster spawn generation was not assigned.");

		const float radius = spawner.monsterSnapshot.collisionRadius;
		Require(spawn.position.x >= spawner.areaMinimum.x + radius && spawn.position.x < spawner.areaMaximum.x - radius &&
					spawn.position.y >= spawner.areaMinimum.y + radius && spawn.position.y < spawner.areaMaximum.y - radius,
			"Monster center or collision radius escaped its SpawnArea.");

		FSectorId expectedSectorId = kInvalidSectorId;
		Require(map.GetSectorGrid().TryResolveSector(spawn.position, expectedSectorId) && expectedSectorId == spawn.sectorId,
			"Monster spawn result contains the wrong SectorId.");
		const FMonsterEntity* const monster = map.FindMonster(spawn.entityId);
		Require(monster != nullptr && monster->GetRuntimeSnapshot() == spawner.monsterSnapshot &&
					monster->GetCurrentHp() == spawner.monsterSnapshot.maxHp && monster->GetSectorId() == spawn.sectorId,
			"Committed Monster state does not match its runtime Snapshot.");
	}

	void TestMonsterInitialSpawnAndCapacity()
	{
		const SMapDefinition definition = MakeDefinition(100, 100, 10);
		std::unique_ptr<FMapInstance> map = CreateMap(600, definition);
		const SMonsterSpawnerRuntimeDefinition spawner = MakeMonsterSpawner(definition.mapDataId);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(0x1234'5678ull, spawners, error), error);
		Require(map->GetMonsterCount() == 0, "Configuring Monster spawning changed committed World state before a Tick.");

		const SMapTickResult firstTick = map->Tick();
		Require(firstTick.result == EMapTickResult::Completed, firstTick.failureReason);
		Require(firstTick.spawnResults.size() == spawner.initialSpawnCount && map->GetMonsterCount() == spawner.initialSpawnCount,
			"The first successful Commit did not create the initial Monster population.");
		for (std::size_t index = 0; index < firstTick.spawnResults.size(); ++index)
		{
			VerifyCommittedMonsterSpawn(*map, spawner, firstTick.spawnResults[index]);
			for (std::size_t otherIndex = index + 1; otherIndex < firstTick.spawnResults.size(); ++otherIndex)
			{
				Require(firstTick.spawnResults[index].entityId != firstTick.spawnResults[otherIndex].entityId,
					"Two Monsters received the same EntityId.");
			}
		}

		const SMapTickResult capacityTick = map->Tick();
		Require(capacityTick.result == EMapTickResult::Completed && capacityTick.spawnResults.empty() &&
					map->GetMonsterCount() == spawner.maxAliveCount,
			"A full Monster Spawner exceeded MaxAliveCount on the next Commit.");
	}

	std::vector<SMonsterSpawnResult> RunDeterministicMonsterSpawn(
		const FMapInstanceId mapInstanceId,
		const std::uint64_t seed)
	{
		const SMapDefinition definition = MakeDefinition(100, 100, 10);
		std::unique_ptr<FMapInstance> map = CreateMap(mapInstanceId, definition);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{MakeMonsterSpawner(definition.mapDataId)};
		std::string error;
		Require(map->ConfigureMonsterSpawning(seed, spawners, error), error);
		const SMapTickResult tick = map->Tick();
		Require(
			tick.result == EMapTickResult::Completed && tick.spawnResults.size() == spawners.front().initialSpawnCount, tick.failureReason);
		return tick.spawnResults;
	}

	void TestMonsterSpawnDeterminismAndMapIsolation()
	{
		constexpr std::uint64_t seed = 0xA5A5'5A5A'1234'5678ull;
		const std::vector<SMonsterSpawnResult> first = RunDeterministicMonsterSpawn(610, seed);
		const std::vector<SMonsterSpawnResult> repeated = RunDeterministicMonsterSpawn(610, seed);
		Require(first == repeated, "The same seed and MapInstanceId produced different Monster spawns.");

		const std::vector<SMonsterSpawnResult> otherMap = RunDeterministicMonsterSpawn(611, seed);
		Require(first.size() == otherMap.size(), "MapInstanceId changed the initial Monster population size.");
		const bool hasDifferentPosition = !std::equal(first.begin(),
			first.end(),
			otherMap.begin(),
			[](const SMonsterSpawnResult& lhs, const SMonsterSpawnResult& rhs)
			{
				return lhs.position == rhs.position;
			});
		Require(hasDifferentPosition, "Different MapInstanceIds reused the same deterministic Monster positions.");
	}

	void TestMonsterSpawnerStableOrder()
	{
		const SMapDefinition definition = MakeDefinition(100, 100, 10);
		std::unique_ptr<FMapInstance> map = CreateMap(615, definition);
		const SMonsterSpawnerRuntimeDefinition higherSpawner =
			MakeMonsterSpawner(definition.mapDataId, 3'102, 1'102, {50.0f, 50.0f}, {80.0f, 80.0f}, 1, 1, 2);
		const SMonsterSpawnerRuntimeDefinition lowerSpawner =
			MakeMonsterSpawner(definition.mapDataId, 3'101, 1'101, {10.0f, 10.0f}, {40.0f, 40.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> reversedSpawners{higherSpawner, lowerSpawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(17, reversedSpawners, error), error);

		const SMapTickResult firstTick = map->Tick();
		Require(firstTick.result == EMapTickResult::Completed && firstTick.spawnResults.size() == 2, firstTick.failureReason);
		Require(firstTick.spawnResults[0].spawnerDataId == lowerSpawner.spawnerDataId &&
					firstTick.spawnResults[1].spawnerDataId == higherSpawner.spawnerDataId &&
					firstTick.spawnResults[0].entityId == (kMapLocalEntityIdBit | 1u) &&
					firstTick.spawnResults[1].entityId == (kMapLocalEntityIdBit | 2u),
			"Spawner configuration input order changed deterministic Spawn Commit order or EntityId allocation.");
	}

	void TestMonsterAiSpawnBoundaryNearestTieAndAttackRange()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		definition.tickRateHz = 20;
		std::unique_ptr<FMapInstance> map = CreateMap(616, definition);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'201, 1'201, {48.0f, 48.0f}, {52.0f, 52.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(23, spawners, error), error);

		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1 && spawnTick.monsterAiResults.empty(),
			"The initial Spawn Tick executed AI for a Monster that was not committed yet.");
		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetTargetEntityId() == kInvalidEntityId && monster->GetAiState() == EMonsterAiState::Idle &&
					monster->GetMoveState() == EMoveState::Stop,
			"A newly committed Monster did not start from the Idle state.");

		const SVector2 spawnPosition = monster->GetPosition();
		AddPlayer(*map, 20, {spawnPosition.x - 8.0f, spawnPosition.y});
		AddPlayer(*map, 10, {spawnPosition.x + 8.0f, spawnPosition.y});
		const float fullStep = spawner.monsterSnapshot.moveSpeed / static_cast<float>(definition.tickRateHz);

		const SMapTickResult firstChaseTick = map->Tick();
		const SMonsterAiResult* firstChase = FindMonsterAiResult(firstChaseTick, monsterEntityId);
		Require(firstChaseTick.result == EMapTickResult::Completed && firstChase != nullptr && firstChase->targetEntityId == 10 &&
					firstChase->aiState == EMonsterAiState::Chase && firstChase->moveState == EMoveState::Start,
			"Equal-distance target selection did not choose the lowest Player EntityId.");
		Require(NearlyEqual(firstChase->acceptedPosition.x, spawnPosition.x + fullStep) &&
					NearlyEqual(firstChase->acceptedPosition.y, spawnPosition.y) && NearlyEqual(firstChase->direction.x, 1.0f) &&
					NearlyEqual(firstChase->direction.y, 0.0f),
			"Monster chase movement did not use MoveSpeed / TickRateHz.");

		const SMapTickResult rangeClampTick = map->Tick();
		const SMonsterAiResult* rangeClamp = FindMonsterAiResult(rangeClampTick, monsterEntityId);
		Require(rangeClampTick.result == EMapTickResult::Completed && rangeClamp != nullptr && rangeClamp->targetEntityId == 10 &&
					rangeClamp->aiState == EMonsterAiState::Chase,
			rangeClampTick.failureReason);
		const FPlayerEntity* target = map->FindPlayer(10);
		Require(target != nullptr && NearlyEqual(std::sqrt(GetDistanceSquared(rangeClamp->acceptedPosition, target->GetPosition())),
										 spawner.monsterSnapshot.attackRange),
			"Monster chase movement overshot or stopped outside AttackRange.");

		const SVector2 attackReadyPosition = rangeClamp->acceptedPosition;
		const SMapTickResult attackReadyTick = map->Tick();
		const SMonsterAiResult* attackReady = FindMonsterAiResult(attackReadyTick, monsterEntityId);
		Require(attackReadyTick.result == EMapTickResult::Completed && attackReady != nullptr && attackReady->targetEntityId == 10 &&
					attackReady->aiState == EMonsterAiState::AttackReady && attackReady->moveState == EMoveState::Stop &&
					attackReady->acceptedPosition == attackReadyPosition,
			"Monster did not transition to AttackReady without moving inside AttackRange.");

		std::vector<SVisibilityEvent> visibilityEvents;
		Require(map->RemovePlayer(10, visibilityEvents, error), error);
		Require(map->RemovePlayer(20, visibilityEvents, error), error);
		const SMapTickResult returnTick = map->Tick();
		const SMonsterAiResult* returning = FindMonsterAiResult(returnTick, monsterEntityId);
		Require(returnTick.result == EMapTickResult::Completed && returning != nullptr && returning->targetEntityId == kInvalidEntityId &&
					returning->aiState == EMonsterAiState::Return && returning->moveState == EMoveState::Start &&
					returning->acceptedPosition != attackReadyPosition,
			"Monster did not clear a lost target and start returning to its Spawn position.");

		const SMapTickResult idleTick = map->Tick();
		const SMonsterAiResult* idle = FindMonsterAiResult(idleTick, monsterEntityId);
		Require(idleTick.result == EMapTickResult::Completed && idle != nullptr && idle->targetEntityId == kInvalidEntityId &&
					idle->aiState == EMonsterAiState::Idle && idle->moveState == EMoveState::Stop &&
					idle->acceptedPosition == spawnPosition,
			"Monster did not finish returning at its exact Spawn position.");
	}

	void TestMonsterAggroTypeAndLeashReturn()
	{
		SMapDefinition definition = MakeDefinition(100, 40, 10);
		definition.tickRateHz = 20;
		definition.maxAcceptedPositionError = 100.0f;
		std::unique_ptr<FMapInstance> map = CreateMap(621, definition);
		SMonsterSpawnerRuntimeDefinition aggressiveSpawner =
			MakeMonsterSpawner(definition.mapDataId, 3'205, 1'205, {20.0f, 10.0f}, {24.0f, 14.0f}, 1, 1, 2);
		aggressiveSpawner.monsterSnapshot.leashRadius = 12.0f;
		const std::vector<SMonsterSpawnerRuntimeDefinition> aggressiveSpawners{aggressiveSpawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(41, aggressiveSpawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1, spawnTick.failureReason);

		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetSpawnPosition() == monster->GetPosition(),
			"Monster did not preserve its exact randomized Spawn position as the return origin.");
		const SVector2 spawnPosition = monster->GetSpawnPosition();
		AddPlayer(*map, 1, {spawnPosition.x + aggressiveSpawner.monsterSnapshot.aggroRadius, spawnPosition.y});

		const SMapTickResult acquireTick = map->Tick();
		const SMonsterAiResult* acquire = FindMonsterAiResult(acquireTick, monsterEntityId);
		Require(acquireTick.result == EMapTickResult::Completed && acquire != nullptr && acquire->targetEntityId == 1 &&
					acquire->aiState == EMonsterAiState::Chase,
			"Aggressive Monster did not acquire a Player at the AggroRadius boundary.");

		Require(map->QueueMove({1, 1, EMoveState::Sync, {spawnPosition.x + 10.0f, spawnPosition.y}, {1.0f, 0.0f}}),
			"Inside-Leash Player movement was rejected.");
		Require(map->Tick().result == EMapTickResult::Completed, "Inside-Leash Player movement Tick failed.");
		const SMapTickResult retainedTick = map->Tick();
		const SMonsterAiResult* retained = FindMonsterAiResult(retainedTick, monsterEntityId);
		Require(retainedTick.result == EMapTickResult::Completed && retained != nullptr && retained->targetEntityId == 1 &&
					retained->aiState == EMonsterAiState::Chase,
			"Monster dropped a retained target outside AggroRadius but still inside LeashRadius.");

		Require(map->QueueMove({1,
					2,
					EMoveState::Sync,
					{spawnPosition.x + aggressiveSpawner.monsterSnapshot.leashRadius + 1.0f, spawnPosition.y},
					{1.0f, 0.0f}}),
			"Outside-Leash Player movement was rejected.");
		Require(map->Tick().result == EMapTickResult::Completed, "Outside-Leash Player movement Tick failed.");
		const SMapTickResult returnTick = map->Tick();
		const SMonsterAiResult* returning = FindMonsterAiResult(returnTick, monsterEntityId);
		Require(returnTick.result == EMapTickResult::Completed && returning != nullptr && returning->targetEntityId == kInvalidEntityId &&
					returning->aiState == EMonsterAiState::Return && returning->moveState == EMoveState::Start,
			"Monster did not release a target outside LeashRadius and enter Return.");

		monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Returning Monster disappeared.");
		AddPlayer(*map, 2, {monster->GetPosition().x + 1.0f, monster->GetPosition().y});
		bool reachedSpawn = false;
		for (std::uint32_t tick = 0; tick < 10; ++tick)
		{
			const SMapTickResult result = map->Tick();
			const SMonsterAiResult* ai = FindMonsterAiResult(result, monsterEntityId);
			Require(result.result == EMapTickResult::Completed && ai != nullptr && ai->targetEntityId == kInvalidEntityId,
				"Returning Monster reacquired a nearby Player before reaching its Spawn position.");
			if (ai->aiState == EMonsterAiState::Idle)
			{
				Require(ai->moveState == EMoveState::Stop && ai->acceptedPosition == spawnPosition,
					"Returning Monster did not stop at its exact Spawn position.");
				reachedSpawn = true;
				break;
			}
			Require(ai->aiState == EMonsterAiState::Return && ai->moveState == EMoveState::Start,
				"Return state was not latched until the Monster reached its Spawn position.");
		}
		Require(reachedSpawn, "Monster did not reach its Spawn position within the expected Tick budget.");

		const SMapTickResult reacquireTick = map->Tick();
		const SMonsterAiResult* reacquire = FindMonsterAiResult(reacquireTick, monsterEntityId);
		Require(reacquireTick.result == EMapTickResult::Completed && reacquire != nullptr && reacquire->targetEntityId == 2,
			"Aggressive Monster did not resume target acquisition after Return completed.");

		std::unique_ptr<FMapInstance> passiveMap = CreateMap(622, definition);
		SMonsterSpawnerRuntimeDefinition passiveSpawner =
			MakeMonsterSpawner(definition.mapDataId, 3'206, 1'206, {60.0f, 10.0f}, {64.0f, 14.0f}, 1, 1, 2);
		passiveSpawner.monsterSnapshot.aggroType = EMonsterAggroType::Passive;
		passiveSpawner.monsterSnapshot.leashRadius = 12.0f;
		const std::vector<SMonsterSpawnerRuntimeDefinition> passiveSpawners{passiveSpawner};
		Require(passiveMap->ConfigureMonsterSpawning(43, passiveSpawners, error), error);
		const SMapTickResult passiveSpawnTick = passiveMap->Tick();
		Require(passiveSpawnTick.result == EMapTickResult::Completed && passiveSpawnTick.spawnResults.size() == 1,
			passiveSpawnTick.failureReason);
		const FEntityId passiveMonsterEntityId = passiveSpawnTick.spawnResults.front().entityId;
		const FMonsterEntity* passiveMonster = passiveMap->FindMonster(passiveMonsterEntityId);
		Require(passiveMonster != nullptr, "Passive Monster was not spawned.");
		AddPlayer(*passiveMap, 10, {passiveMonster->GetPosition().x + 1.0f, passiveMonster->GetPosition().y});
		const SMapTickResult passiveTick = passiveMap->Tick();
		const SMonsterAiResult* passiveAi = FindMonsterAiResult(passiveTick, passiveMonsterEntityId);
		Require(passiveTick.result == EMapTickResult::Completed && passiveAi != nullptr && passiveAi->targetEntityId == kInvalidEntityId &&
					passiveAi->aiState == EMonsterAiState::Idle && passiveAi->moveState == EMoveState::Stop,
			"Passive Monster automatically acquired a nearby Player without being attacked.");
	}

	void TestMonsterBasicAttackDamageCooldownAndVisibility()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		definition.tickRateHz = 10;
		definition.combatPolicy.minimumDamage = 2;
		std::unique_ptr<FMapInstance> map = CreateMap(623, definition);
		SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'207, 1'207, {40.0f, 40.0f}, {44.0f, 44.0f}, 1, 1, 2);
		spawner.monsterSnapshot.attack = 5;
		spawner.monsterSnapshot.attackCooldownMilliseconds = 300;
		spawner.monsterSnapshot.attackCooldownTicks = 3;
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(47, spawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1, spawnTick.failureReason);

		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Attack test Monster was not spawned.");
		AddCombatPlayer(*map, 1, {monster->GetPosition().x + 1.0f, monster->GetPosition().y}, 10, 100);

		const SMapTickResult firstAttack = map->Tick();
		const FPlayerEntity* target = map->FindPlayer(1);
		monster = map->FindMonster(monsterEntityId);
		Require(firstAttack.result == EMapTickResult::Completed && firstAttack.actorAttackResults.size() == 1 &&
					firstAttack.actorAttackResults.front() == SActorAttackResult{monsterEntityId, 1, 2, 8, 10},
			"MinimumDamage was not applied by the first Monster attack.");
		Require(target != nullptr && target->GetCurrentHp() == 8 && monster != nullptr && monster->GetNextAttackTick() == 5,
			"First Monster attack did not atomically commit HP and Cooldown.");
		Require(std::ranges::any_of(firstAttack.actorAttackEvents,
					[monsterEntityId](const SActorAttackEvent& event)
					{
						return event.observerEntityId == 1 && event.attack.attackerEntityId == monsterEntityId &&
							   event.attack.targetEntityId == 1;
					}),
			"The damaged Player did not receive its own ActorAttack event.");

		const SMapTickResult cooldownTick3 = map->Tick();
		const SMapTickResult cooldownTick4 = map->Tick();
		Require(
			cooldownTick3.actorAttackResults.empty() && cooldownTick4.actorAttackResults.empty() && map->FindPlayer(1)->GetCurrentHp() == 8,
			"Monster attacked before the exact Cooldown Tick.");
		const SMapTickResult secondAttack = map->Tick();
		Require(secondAttack.tickIndex == 5 && secondAttack.actorAttackResults.size() == 1 &&
					secondAttack.actorAttackResults.front().damage == 2 && map->FindPlayer(1)->GetCurrentHp() == 6 &&
					map->FindMonster(monsterEntityId)->GetNextAttackTick() == 8,
			"Monster did not attack again on the exact Cooldown Tick.");
	}

	void TestMultipleMonsterAttacksUseStableCommitOrder()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 100);
		std::unique_ptr<FMapInstance> map = CreateMap(624, definition);
		SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'208, 1'208, {40.0f, 40.0f}, {60.0f, 60.0f}, 2, 2, 2);
		spawner.monsterSnapshot.attack = 5;
		spawner.monsterSnapshot.aggroRadius = 40.0f;
		spawner.monsterSnapshot.leashRadius = 40.0f;
		spawner.monsterSnapshot.attackRange = 30.0f;
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(53, spawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 2, spawnTick.failureReason);
		AddCombatPlayer(*map, 1, {50.0f, 50.0f}, 6);

		const SMapTickResult attackTick = map->Tick();
		Require(attackTick.result == EMapTickResult::Completed && attackTick.actorAttackResults.size() == 2,
			"Two simultaneous Monster attacks were not committed.");
		Require(attackTick.actorAttackResults[0].attackerEntityId < attackTick.actorAttackResults[1].attackerEntityId &&
					attackTick.actorAttackResults[0].damage == 5 && attackTick.actorAttackResults[0].targetCurrentHp == 1 &&
					attackTick.actorAttackResults[1].damage == 1 && attackTick.actorAttackResults[1].targetCurrentHp == 0 &&
					map->FindPlayer(1)->GetCurrentHp() == 0,
			"Simultaneous attacks did not use stable attacker order and clamped HP subtraction.");

		const SMapTickResult deadTargetTick = map->Tick();
		Require(deadTargetTick.result == EMapTickResult::Completed && deadTargetTick.actorAttackResults.empty(),
			"A Player at zero HP remained a valid Monster attack target.");
		Require(!map->QueueMove({1, 1, EMoveState::Start, {51.0f, 50.0f}, {1.0f, 0.0f}}),
			"A Player at zero HP was allowed to enqueue movement.");
	}

	void TestMonsterAttackRollsBackOnDownstreamFailure()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		std::unique_ptr<FMapInstance> map = CreateMap(625, definition);
		SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'209, 1'209, {40.0f, 40.0f}, {44.0f, 44.0f}, 1, 1, 2);
		spawner.monsterSnapshot.attack = 5;
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(59, spawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1, spawnTick.failureReason);
		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Rollback test Monster was not spawned.");
		AddCombatPlayer(*map, 1, {monster->GetPosition().x + 1.0f, monster->GetPosition().y}, 20);
		const std::uint64_t stateHashBefore = map->GetStateHash();

		FMapInstanceTestAccess::InjectNextMonsterSpawnCommitFailure(*map);
		const SMapTickResult failedTick = map->Tick();
		Require(
			failedTick.result == EMapTickResult::Failed && failedTick.actorAttackResults.empty() && failedTick.actorAttackEvents.empty(),
			"Downstream failure exposed an attack result that was rolled back.");
		Require(map->FindPlayer(1)->GetCurrentHp() == 20 && map->FindMonster(monsterEntityId)->GetNextAttackTick() == 0 &&
					map->GetTickIndex() == 1 && map->GetStateHash() == stateHashBefore,
			"Downstream failure did not restore Player HP and Monster Cooldown.");

		const SMapTickResult recoveredTick = map->Tick();
		Require(recoveredTick.result == EMapTickResult::Completed && recoveredTick.tickIndex == 2 &&
					recoveredTick.actorAttackResults.size() == 1 && map->FindPlayer(1)->GetCurrentHp() == 15,
			"Monster attack did not retry the rolled-back logical Tick.");
	}

	void TestMonsterAttackWaitsForAsyncOwnerCommit()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		FPendingSectorExecutor* executor = nullptr;
		std::unique_ptr<FMapInstance> map = CreatePendingMap(626, definition, executor);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'210, 1'210, {40.0f, 40.0f}, {44.0f, 44.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(61, spawners, error), error);
		Require(map->Tick().result == EMapTickResult::Pending, "Async Spawn Tick did not enter Pending state.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Async Spawn completion was rejected.");
		const SMapTickResult spawnCommit = map->Tick();
		Require(spawnCommit.result == EMapTickResult::Completed && spawnCommit.spawnResults.size() == 1, spawnCommit.failureReason);

		const FEntityId monsterEntityId = spawnCommit.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Async attack test Monster was not spawned.");
		AddCombatPlayer(*map, 1, {monster->GetPosition().x + 1.0f, monster->GetPosition().y}, 5);
		const SMapTickResult pendingAttack = map->Tick();
		Require(pendingAttack.result == EMapTickResult::Pending && map->FindPlayer(1)->GetCurrentHp() == 5 &&
					map->FindMonster(monsterEntityId)->GetNextAttackTick() == 0,
			"Pending attack Tick changed committed HP or Cooldown.");
		Require(map->QueueMove({1, 1, EMoveState::Start, map->FindPlayer(1)->GetPosition(), {1.0f, 0.0f}}),
			"A next-Tick Move could not be queued while the attack Tick was pending.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Async attack completion was rejected.");
		Require(map->FindPlayer(1)->GetCurrentHp() == 5 && map->FindMonster(monsterEntityId)->GetNextAttackTick() == 0,
			"Completion delivery changed combat state outside the Owner Tick.");
		const SMapTickResult attackCommit = map->Tick();
		Require(attackCommit.result == EMapTickResult::Completed && attackCommit.actorAttackResults.size() == 1 &&
					attackCommit.actorDeathResults.size() == 1 &&
					attackCommit.rejectedMoveRequests == std::vector<SMoveRequestIdentity>{{1, 1}} &&
					map->FindPlayer(1)->GetCurrentHp() == 0 && map->GetPendingMoveCount() == 0 &&
					map->FindMonster(monsterEntityId)->GetNextAttackTick() > attackCommit.tickIndex,
			"Owner Tick did not atomically commit Death and reject the queued next-Tick Move.");
	}

	void TestPlayerRandomFirstSpawnIsDeterministicAndCollisionFree()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		definition.playerSpawnAreaMinimum = {20.0f, 20.0f};
		definition.playerSpawnAreaMaximum = {80.0f, 80.0f};
		std::unique_ptr<FMapInstance> firstMap = CreateMap(632, definition);
		std::unique_ptr<FMapInstance> replayMap = CreateMap(632, definition);

		const SPlayerRuntimeSnapshot firstSnapshot = MakeCombatPlayerRuntimeSnapshot(1, 100);
		const SPlayerRuntimeSnapshot secondSnapshot = MakeCombatPlayerRuntimeSnapshot(2, 100);
		std::vector<SVisibilityEvent> firstEvents;
		std::vector<SVisibilityEvent> replayEvents;
		std::string error;
		Require(firstMap->AddPlayerAtRandomSpawn(1, 10'001, {0.0f, 1.0f}, firstSnapshot, firstEvents, error), error);
		Require(replayMap->AddPlayerAtRandomSpawn(1, 10'001, {0.0f, 1.0f}, firstSnapshot, replayEvents, error), error);
		const FPlayerEntity* const firstPlayer = firstMap->FindPlayer(1);
		const FPlayerEntity* const replayPlayer = replayMap->FindPlayer(1);
		Require(firstPlayer != nullptr && replayPlayer != nullptr && firstPlayer->GetPosition() == replayPlayer->GetPosition(),
			"The same Map/Entity/Life key did not reproduce the same first Spawn position.");
		Require(firstPlayer->GetPosition().x >= 21.0f && firstPlayer->GetPosition().x < 79.0f && firstPlayer->GetPosition().y >= 21.0f &&
					firstPlayer->GetPosition().y < 79.0f,
			"Random first Spawn ignored the configured Spawn Area or collision radius.");

		std::vector<SVisibilityEvent> secondEvents;
		Require(firstMap->AddPlayerAtRandomSpawn(2, 10'002, {0.0f, 1.0f}, secondSnapshot, secondEvents, error), error);
		const FPlayerEntity* const secondPlayer = firstMap->FindPlayer(2);
		Require(secondPlayer != nullptr && GetDistanceSquared(firstPlayer->GetPosition(), secondPlayer->GetPosition()) >= 4.0f,
			"Random first Spawn overlapped another Player collision circle.");
		Require(firstMap->GetSectorGrid().GetEntitySectorId(2) == std::optional<FSectorId>(secondPlayer->GetSectorId()),
			"Random first Spawn did not register exactly one matching Sector owner.");
	}

	void TestPlayerDeathRollbackAndExactRandomRespawn()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		definition.playerSpawnAreaMinimum = {70.0f, 70.0f};
		definition.playerSpawnAreaMaximum = {90.0f, 90.0f};
		definition.playerRespawnDelayTicks = 3;
		std::unique_ptr<FMapInstance> map = CreateMap(633, definition);
		SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'211, 1'211, {40.0f, 40.0f}, {60.0f, 60.0f}, 2, 2, 2);
		spawner.monsterSnapshot.attack = 5;
		spawner.monsterSnapshot.aggroRadius = 20.0f;
		spawner.monsterSnapshot.leashRadius = 40.0f;
		spawner.monsterSnapshot.attackRange = 20.0f;
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(67, spawners, error),
			error.empty() ? "ConfigureMonsterSpawning failed without a diagnostic." : error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 2,
			spawnTick.failureReason.empty() ? "Initial lifecycle Spawn Tick did not create two Monsters." : spawnTick.failureReason);
		AddCombatPlayer(*map, 1, {50.0f, 50.0f}, 6);

		const std::uint64_t preAttackHash = map->GetStateHash();
		FMapInstanceTestAccess::InjectNextMonsterSpawnCommitFailure(*map);
		const SMapTickResult failedAttack = map->Tick();
		const FPlayerEntity* player = map->FindPlayer(1);
		Require(failedAttack.result == EMapTickResult::Failed && failedAttack.actorDeathResults.empty() &&
					failedAttack.actorRespawnResults.empty() && player != nullptr && player->IsAlive() && player->GetCurrentHp() == 6 &&
					player->GetLifeRevision() == 1 && player->GetRespawnDueTick() == 0 && map->GetTickIndex() == 1 &&
					map->GetStateHash() == preAttackHash,
			"A downstream failure exposed or retained a tentative Player Death.");

		const SMapTickResult deathTick = map->Tick();
		player = map->FindPlayer(1);
		Require(deathTick.result == EMapTickResult::Completed && deathTick.tickIndex == 2 && deathTick.actorDeathResults.size() == 1 &&
					deathTick.actorDeathEvents.size() == 1 && player != nullptr && !player->IsAlive() && player->GetCurrentHp() == 0 &&
					player->GetLifeRevision() == 1 && player->GetRespawnDueTick() == 5 && player->GetMoveState() == EMoveState::Stop,
			"A lethal attack did not commit exactly one Player Death and self notification.");
		Require(!map->QueueMove({1, 1, EMoveState::Start, {51.0f, 50.0f}, {1.0f, 0.0f}}), "A dead Player was allowed to enqueue movement.");

		const SMapTickResult waitTick3 = map->Tick();
		const SMapTickResult waitTick4 = map->Tick();
		Require(waitTick3.actorRespawnResults.empty() && waitTick4.actorRespawnResults.empty() && map->FindPlayer(1) != nullptr &&
					!map->FindPlayer(1)->IsAlive(),
			"Player Respawn occurred before the configured due Tick.");

		const SMapTickResult respawnTick = map->Tick();
		player = map->FindPlayer(1);
		Require(respawnTick.result == EMapTickResult::Completed && respawnTick.tickIndex == 5 &&
					respawnTick.actorRespawnResults.size() == 1 && respawnTick.actorRespawnEvents.size() == 1 && player != nullptr &&
					player->IsAlive() && player->GetLifeRevision() == 2 && player->GetCurrentHp() == 6 && player->GetRespawnDueTick() == 0,
			"Player did not Respawn with a new LifeRevision and full HP on the exact due Tick.");
		Require(player->GetPosition().x >= 71.0f && player->GetPosition().x < 89.0f && player->GetPosition().y >= 71.0f &&
					player->GetPosition().y < 89.0f &&
					map->GetSectorGrid().GetEntitySectorId(1) == std::optional<FSectorId>(player->GetSectorId()),
			"Player Respawn position or Sector ownership is outside the configured random Spawn Area.");
		Require(map->QueueMove({1, 2, EMoveState::Start, player->GetPosition(), {1.0f, 0.0f}}),
			"A Respawned Player could not enqueue movement.");
	}

	const SRejectedPlayerAttack* FindRejectedPlayerAttack(
		const SMapTickResult& tick,
		const FEntityId attackerEntityId,
		const FAttackSequence attackSequence)
	{
		const auto iterator = std::ranges::find_if(tick.rejectedAttackRequests,
			[&](const SRejectedPlayerAttack& rejected)
			{
				return rejected.request.attackerEntityId == attackerEntityId && rejected.request.attackSequence == attackSequence;
			});
		return iterator == tick.rejectedAttackRequests.end() ? nullptr : &*iterator;
	}

	void TestPlayerBasicAttackLifecycleAndValidation()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 10);
		definition.combatPolicy.playerBasicAttackRange = 8.0f;
		auto map = CreateMap(7'101, definition);
		SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 7'001, 1'001, {20.0f, 20.0f}, {40.0f, 40.0f}, 1, 1, 2);
		spawner.monsterSnapshot = MakeMonsterRuntimeSnapshot(1'001, EMonsterType::Normal, 10, EMonsterAggroType::Passive);
		spawner.monsterSnapshot.defense = 1;
		std::string error;
		Require(map->ConfigureMonsterSpawning(11, std::span(&spawner, 1), error), error);

		const SMapTickResult initialTick = map->Tick();
		Require(initialTick.result == EMapTickResult::Completed && initialTick.spawnResults.size() == 1,
			"Player attack test Monster did not spawn.");
		const FEntityId monsterEntityId = initialTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Spawned Player attack target is missing.");
		const SVector2 monsterPosition = monster->GetPosition();
		AddCombatPlayer(*map, 1, {monsterPosition.x + 1.0f, monsterPosition.y}, 100, 0, 5);
		AddCombatPlayer(*map, 2, {monsterPosition.x + 1.5f, monsterPosition.y}, 100, 0, 20);
		AddCombatPlayer(*map, 3, {monsterPosition.x + 2.0f, monsterPosition.y}, 100, 0, 20);
		AddCombatPlayer(*map, 4, {90.0f, 90.0f}, 100, 0, 20);

		Require(map->QueuePlayerAttack({1, 1, monsterEntityId}), "Valid Player attack was rejected at enqueue.");
		Require(map->QueuePlayerAttack({1, 2, monsterEntityId}), "Ordered Player attack was coalesced instead of queued.");
		Require(map->QueuePlayerAttack({1, 3, 999'999}), "Invalid target must be reported by the Tick result.");
		Require(map->QueuePlayerAttack({4, 1, monsterEntityId}), "Out-of-range attack must be reported by the Tick result.");
		Require(!map->QueuePlayerAttack({1, 3, monsterEntityId}) && !map->QueuePlayerAttack({1, 2, monsterEntityId}),
			"Duplicate or stale Player attack Sequence was accepted.");

		const SMapTickResult firstAttackTick = map->Tick();
		Require(firstAttackTick.result == EMapTickResult::Completed && firstAttackTick.consumedAttackRequests.size() == 4 &&
					firstAttackTick.playerAttackResults.size() == 1 && firstAttackTick.rejectedAttackRequests.size() == 3,
			"Consumed Player attacks did not resolve exactly once as Success or Rejection.");
		const SRejectedPlayerAttack* cooldown = FindRejectedPlayerAttack(firstAttackTick, 1, 2);
		const SRejectedPlayerAttack* invalidTarget = FindRejectedPlayerAttack(firstAttackTick, 1, 3);
		const SRejectedPlayerAttack* outOfRange = FindRejectedPlayerAttack(firstAttackTick, 4, 1);
		Require(cooldown != nullptr && cooldown->reason == EPlayerAttackRejectReason::Cooldown && invalidTarget != nullptr &&
					invalidTarget->reason == EPlayerAttackRejectReason::InvalidTarget && outOfRange != nullptr &&
					outOfRange->reason == EPlayerAttackRejectReason::OutOfRange,
			"Player attack validation reported an incorrect rejection reason.");
		monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetCurrentHp() == 6 && monster->GetTargetEntityId() == 1,
			"Player damage or passive Monster retaliation target was not committed.");

		Require(map->QueuePlayerAttack({2, 1, monsterEntityId}) && map->QueuePlayerAttack({3, 1, monsterEntityId}),
			"Simultaneous lethal Player attacks were not queued.");
		const SMapTickResult lethalTick = map->Tick();
		Require(lethalTick.result == EMapTickResult::Completed && lethalTick.consumedAttackRequests.size() == 2 &&
					lethalTick.playerAttackResults.size() == 1 && lethalTick.rejectedAttackRequests.size() == 1 &&
					lethalTick.actorDeathResults.size() == 1 && lethalTick.actorDeathResults.front().entityId == monsterEntityId &&
					lethalTick.actorDeathResults.front().killerEntityId == 2 &&
					lethalTick.actorDeathResults.front().lifeRevision == initialTick.spawnResults.front().spawnGeneration &&
					map->FindMonster(monsterEntityId) == nullptr && !lethalTick.actorDeathEvents.empty(),
			"Simultaneous lethal attacks did not produce exactly one Kill and a visible Death event.");
		const SRejectedPlayerAttack* lostLethalRace = FindRejectedPlayerAttack(lethalTick, 3, 1);
		Require(lostLethalRace != nullptr && lostLethalRace->reason == EPlayerAttackRejectReason::TargetDead,
			"The second simultaneous lethal attack was not rejected as TargetDead.");

		const SMapTickResult beforeRespawn = map->Tick();
		const SMapTickResult respawnTick = map->Tick();
		Require(beforeRespawn.spawnResults.empty() && respawnTick.spawnResults.size() == 1 &&
					respawnTick.spawnResults.front().entityId != monsterEntityId &&
					respawnTick.spawnResults.front().spawnGeneration > initialTick.spawnResults.front().spawnGeneration,
			"Killed Monster did not respawn on the configured exact Tick with a new identity.");
	}

	void TestPlayerAttackDeadAttackerAndAtomicRollback()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 10);
		definition.combatPolicy.playerBasicAttackRange = 8.0f;
		auto rollbackMap = CreateMap(7'102, definition);
		SMonsterSpawnerRuntimeDefinition rollbackSpawner =
			MakeMonsterSpawner(definition.mapDataId, 7'002, 1'002, {20.0f, 20.0f}, {24.0f, 24.0f}, 1, 1, 2);
		rollbackSpawner.monsterSnapshot = MakeMonsterRuntimeSnapshot(1'002, EMonsterType::Normal, 1, EMonsterAggroType::Passive);
		rollbackSpawner.monsterSnapshot.defense = 0;
		std::string error;
		Require(rollbackMap->ConfigureMonsterSpawning(12, std::span(&rollbackSpawner, 1), error), error);
		const SMapTickResult initialTick = rollbackMap->Tick();
		const FEntityId targetEntityId = initialTick.spawnResults.front().entityId;
		const SVector2 targetPosition = rollbackMap->FindMonster(targetEntityId)->GetPosition();
		AddCombatPlayer(*rollbackMap, 10, {targetPosition.x + 1.0f, targetPosition.y}, 100, 0, 5);
		const std::uint64_t stateBeforeAttack = rollbackMap->GetStateHash();
		Require(rollbackMap->QueuePlayerAttack({10, 1, targetEntityId}), "Rollback test attack was not queued.");
		FMapInstanceTestAccess::InjectNextMonsterSpawnCommitFailure(*rollbackMap);
		const SMapTickResult failedTick = rollbackMap->Tick();
		const FMonsterEntity* restoredTarget = rollbackMap->FindMonster(targetEntityId);
		const FPlayerEntity* restoredAttacker = rollbackMap->FindPlayer(10);
		Require(failedTick.result == EMapTickResult::Failed && failedTick.playerAttackResults.empty() &&
					failedTick.actorDeathResults.empty() && rollbackMap->GetTickIndex() == 1 &&
					rollbackMap->GetStateHash() == stateBeforeAttack && restoredTarget != nullptr && restoredTarget->GetCurrentHp() == 1 &&
					restoredAttacker != nullptr && restoredAttacker->GetNextBasicAttackTick() == 0 &&
					rollbackMap->GetSectorGrid().ContainsEntity(restoredTarget->GetSectorId(), targetEntityId),
			"Downstream failure did not atomically restore Monster HP, cooldown, AI, Registry, Sector, and Spawner state.");

		Require(rollbackMap->QueuePlayerAttack({10, 2, targetEntityId}), "Attack did not recover after rollback.");
		const SMapTickResult recoveredTick = rollbackMap->Tick();
		Require(recoveredTick.result == EMapTickResult::Completed && recoveredTick.playerAttackResults.size() == 1 &&
					rollbackMap->FindMonster(targetEntityId) == nullptr,
			"Player attack did not recover after an injected rollback.");

		auto deadMap = CreateMap(7'103, definition);
		SMonsterSpawnerRuntimeDefinition aggressiveSpawner =
			MakeMonsterSpawner(definition.mapDataId, 7'003, 1'003, {20.0f, 20.0f}, {24.0f, 24.0f}, 1, 1, 2);
		aggressiveSpawner.monsterSnapshot = MakeMonsterRuntimeSnapshot(1'003, EMonsterType::Normal, 10, EMonsterAggroType::Aggressive);
		Require(deadMap->ConfigureMonsterSpawning(13, std::span(&aggressiveSpawner, 1), error), error);
		const SMapTickResult aggressiveSpawnTick = deadMap->Tick();
		const FEntityId aggressiveMonsterId = aggressiveSpawnTick.spawnResults.front().entityId;
		const SVector2 aggressivePosition = deadMap->FindMonster(aggressiveMonsterId)->GetPosition();
		AddCombatPlayer(*deadMap, 20, {aggressivePosition.x + 1.0f, aggressivePosition.y}, 1, 0, 5);
		const SMapTickResult deathTick = deadMap->Tick();
		Require(
			deathTick.actorDeathResults.size() == 1 && !deadMap->FindPlayer(20)->IsAlive(), "Dead-attacker test Player was not killed.");
		Require(deadMap->QueuePlayerAttack({20, 1, aggressiveMonsterId}), "Dead Player attack should be resolved by the Tick.");
		const SMapTickResult deadAttackTick = deadMap->Tick();
		const SRejectedPlayerAttack* deadAttacker = FindRejectedPlayerAttack(deadAttackTick, 20, 1);
		Require(deadAttackTick.consumedAttackRequests.size() == 1 && deadAttackTick.playerAttackResults.empty() &&
					deadAttacker != nullptr && deadAttacker->reason == EPlayerAttackRejectReason::AttackerDead,
			"Dead Player attack was not rejected with AttackerDead.");
	}

	void TestMonsterAiDeferredSectorTransfer()
	{
		SMapDefinition definition = MakeDefinition(30, 10, 10);
		definition.tickRateHz = 20;
		std::unique_ptr<FMapInstance> map = CreateMap(617, definition);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'202, 1'202, {6.5f, 3.0f}, {9.5f, 7.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(29, spawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1, spawnTick.failureReason);

		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetSectorId() == 0, "Sector-transfer Monster did not spawn in Sector 0.");
		const SVector2 spawnPosition = monster->GetSpawnPosition();
		AddPlayer(*map, 1, {15.0f, monster->GetPosition().y});

		const SMapTickResult transferTick = map->Tick();
		const SMonsterAiResult* transfer = FindMonsterAiResult(transferTick, monsterEntityId);
		Require(transferTick.result == EMapTickResult::Completed && transfer != nullptr && transfer->aiState == EMonsterAiState::Chase &&
					transfer->previousSectorId == 0 && transfer->currentSectorId == 1 && transfer->acceptedPosition.x >= 10.0f,
			"Monster chase did not produce the expected deferred Sector transfer.");
		monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetSectorId() == 1 &&
					map->GetSectorGrid().GetEntitySectorId(monsterEntityId) == std::optional<FSectorId>(1) &&
					map->GetSectorGrid().ContainsEntity(1, monsterEntityId) && !map->GetSectorGrid().ContainsEntity(0, monsterEntityId),
			"Owner Commit did not apply the Monster Sector transfer atomically.");

		std::vector<SVisibilityEvent> visibilityEvents;
		Require(map->RemovePlayer(1, visibilityEvents, error), error);
		const SMapTickResult returnTransferTick = map->Tick();
		const SMonsterAiResult* returnTransfer = FindMonsterAiResult(returnTransferTick, monsterEntityId);
		Require(returnTransferTick.result == EMapTickResult::Completed && returnTransfer != nullptr &&
					returnTransfer->targetEntityId == kInvalidEntityId && returnTransfer->aiState == EMonsterAiState::Idle &&
					returnTransfer->moveState == EMoveState::Stop && returnTransfer->previousSectorId == 1 &&
					returnTransfer->currentSectorId == 0 && returnTransfer->acceptedPosition == spawnPosition,
			"Monster return did not produce the expected deferred Sector transfer back to its Spawn position.");
		monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetSectorId() == 0 &&
					map->GetSectorGrid().GetEntitySectorId(monsterEntityId) == std::optional<FSectorId>(0) &&
					map->GetSectorGrid().ContainsEntity(0, monsterEntityId) && !map->GetSectorGrid().ContainsEntity(1, monsterEntityId),
			"Owner Commit did not apply the Monster return Sector transfer atomically.");
	}

	void TestMonsterAiWaitsForAsyncCommit()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 20);
		FPendingSectorExecutor* executor = nullptr;
		std::unique_ptr<FMapInstance> map = CreatePendingMap(618, definition, executor);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'203, 1'203, {48.0f, 48.0f}, {52.0f, 52.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(31, spawners, error), error);

		Require(map->Tick().result == EMapTickResult::Pending, "Async Monster Spawn Tick did not start.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Async Monster Spawn completion was rejected.");
		const SMapTickResult spawnCommit = map->Tick();
		Require(spawnCommit.result == EMapTickResult::Completed && spawnCommit.spawnResults.size() == 1, spawnCommit.failureReason);

		const FEntityId monsterEntityId = spawnCommit.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Async AI Monster was not committed.");
		const SVector2 positionBefore = monster->GetPosition();
		AddPlayer(*map, 1, {positionBefore.x + 6.0f, positionBefore.y});

		const SMapTickResult pendingTick = map->Tick();
		monster = map->FindMonster(monsterEntityId);
		Require(pendingTick.result == EMapTickResult::Pending && monster != nullptr && monster->GetPosition() == positionBefore &&
					monster->GetTargetEntityId() == kInvalidEntityId && monster->GetAiState() == EMonsterAiState::Idle,
			"Starting an async AI Tick changed committed Monster state.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Async AI completion was rejected.");
		monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr && monster->GetPosition() == positionBefore && monster->GetTargetEntityId() == kInvalidEntityId &&
					monster->GetAiState() == EMonsterAiState::Idle,
			"Delivering an async AI completion changed Monster state before Owner Commit.");

		const SMapTickResult committedTick = map->Tick();
		const SMonsterAiResult* committedAi = FindMonsterAiResult(committedTick, monsterEntityId);
		monster = map->FindMonster(monsterEntityId);
		Require(committedTick.result == EMapTickResult::Completed && committedAi != nullptr && monster != nullptr &&
					monster->GetPosition() != positionBefore && monster->GetTargetEntityId() == 1 &&
					monster->GetAiState() == EMonsterAiState::Chase,
			"Owner Tick did not commit the completed async Monster AI result.");
	}

	void TestMonsterAiRollsBackOnDownstreamSpawnFailure()
	{
		const SMapDefinition definition = MakeDefinition(100, 100, 20);
		std::unique_ptr<FMapInstance> map = CreateMap(619, definition);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'204, 1'204, {48.0f, 48.0f}, {52.0f, 52.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(37, spawners, error), error);
		const SMapTickResult spawnTick = map->Tick();
		Require(spawnTick.result == EMapTickResult::Completed && spawnTick.spawnResults.size() == 1, spawnTick.failureReason);

		const FEntityId monsterEntityId = spawnTick.spawnResults.front().entityId;
		const FMonsterEntity* monster = map->FindMonster(monsterEntityId);
		Require(monster != nullptr, "Rollback AI Monster was not committed.");
		const SVector2 positionBefore = monster->GetPosition();
		const SVector2 directionBefore = monster->GetDirection();
		const FSectorId sectorBefore = monster->GetSectorId();
		AddPlayer(*map, 1, {positionBefore.x + 6.0f, positionBefore.y});
		const std::uint64_t stateHashBefore = map->GetStateHash();
		const std::uint64_t tickIndexBefore = map->GetTickIndex();

		FMapInstanceTestAccess::InjectNextMonsterSpawnCommitFailure(*map);
		const SMapTickResult failedTick = map->Tick();
		monster = map->FindMonster(monsterEntityId);
		Require(failedTick.result == EMapTickResult::Failed && failedTick.failureReason == "Injected Monster Spawn Commit failure." &&
					failedTick.monsterAiResults.empty() && monster != nullptr,
			"Downstream Spawn failure did not discard the rolled-back Monster AI output.");
		Require(monster->GetPosition() == positionBefore && monster->GetDirection() == directionBefore &&
					monster->GetSectorId() == sectorBefore && monster->GetTargetEntityId() == kInvalidEntityId &&
					monster->GetAiState() == EMonsterAiState::Idle && monster->GetMoveState() == EMoveState::Stop &&
					map->GetTickIndex() == tickIndexBefore && map->GetStateHash() == stateHashBefore,
			"Downstream Spawn failure did not restore the complete Monster AI state.");

		const SMapTickResult recoveredTick = map->Tick();
		monster = map->FindMonster(monsterEntityId);
		Require(recoveredTick.result == EMapTickResult::Completed && FindMonsterAiResult(recoveredTick, monsterEntityId) != nullptr &&
					monster != nullptr && monster->GetPosition() != positionBefore && monster->GetTargetEntityId() == 1 &&
					monster->GetAiState() == EMonsterAiState::Chase,
			"Monster AI did not recover after the downstream Spawn failure.");
	}

	void TestMonsterSpawnWaitsForAsyncCommit()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 10);
		FPendingSectorExecutor* executor = nullptr;
		std::unique_ptr<FMapInstance> map = CreatePendingMap(620, definition, executor);
		const SMonsterSpawnerRuntimeDefinition spawner = MakeMonsterSpawner(definition.mapDataId);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(7, spawners, error), error);

		const SMapTickResult firstStart = map->Tick();
		Require(firstStart.result == EMapTickResult::Pending && map->GetMonsterCount() == 0,
			"Starting an async Tick exposed uncommitted Monsters.");
		Require(map->CompleteTickExecution(executor->MakeFailureCompletion("injected spawn Tick failure")) ==
					EMapTickCompletionResult::Accepted,
			"Injected async Monster Tick failure was rejected.");
		Require(map->GetMonsterCount() == 0, "A failed async completion partially spawned Monsters.");
		const SMapTickResult failedCommit = map->Tick();
		Require(failedCommit.result == EMapTickResult::Failed && failedCommit.spawnResults.empty() && map->GetMonsterCount() == 0,
			"A failed async Tick changed committed Monster state.");

		Require(map->Tick().result == EMapTickResult::Pending, "Recovered async Monster Tick did not start.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Recovered async Monster Tick completion was rejected.");
		Require(map->GetMonsterCount() == 0, "Completion delivery spawned Monsters outside the owner Tick Commit.");
		const SMapTickResult successfulCommit = map->Tick();
		Require(successfulCommit.result == EMapTickResult::Completed && successfulCommit.spawnResults.size() == spawner.initialSpawnCount &&
					map->GetMonsterCount() == spawner.initialSpawnCount,
			"The recovered async Commit did not spawn the initial Monster population exactly once.");

		Require(map->Tick().result == EMapTickResult::Pending, "Second async capacity Tick did not start.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Second async capacity completion was rejected.");
		const SMapTickResult capacityCommit = map->Tick();
		Require(capacityCommit.result == EMapTickResult::Completed && capacityCommit.spawnResults.empty() &&
					map->GetMonsterCount() == spawner.maxAliveCount,
			"Async Commit spawned the initial Monster population more than once.");
	}

	void TestMonsterRemovalAndExactRespawnDelay()
	{
		const SMapDefinition definition = MakeDefinition(100, 100, 10);
		std::unique_ptr<FMapInstance> map = CreateMap(630, definition);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'010, 1'010, {10.0f, 10.0f}, {40.0f, 40.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(11, spawners, error), error);
		const SMapTickResult initialTick = map->Tick();
		Require(initialTick.spawnResults.size() == 1 && map->GetTickIndex() == 1, "Respawn test initial spawn failed.");
		const SMonsterSpawnResult initialSpawn = initialTick.spawnResults.front();

		std::vector<SVisibilityEvent> visibilityEvents;
		Require(map->RemoveMonster(initialSpawn.entityId, visibilityEvents, error), error);
		Require(map->GetMonsterCount() == 0 && map->FindMonster(initialSpawn.entityId) == nullptr,
			"Removed Monster remained in committed Map state.");
		Require(!map->RemoveMonster(initialSpawn.entityId, visibilityEvents, error) && map->GetMonsterCount() == 0,
			"Duplicate Monster removal created another Respawn request or changed committed state.");

		const SMapTickResult beforeDueTick = map->Tick();
		Require(beforeDueTick.result == EMapTickResult::Completed && beforeDueTick.tickIndex == 2 && beforeDueTick.spawnResults.empty() &&
					map->GetMonsterCount() == 0,
			"Monster respawned before RespawnDelayTicks elapsed.");
		const SMapTickResult dueTick = map->Tick();
		Require(dueTick.result == EMapTickResult::Completed && dueTick.tickIndex == 3 && dueTick.spawnResults.size() == 1 &&
					map->GetMonsterCount() == 1,
			"Monster did not respawn on the exact due Commit.");
		const SMonsterSpawnResult& respawn = dueTick.spawnResults.front();
		Require(respawn.entityId != initialSpawn.entityId && respawn.spawnGeneration == initialSpawn.spawnGeneration + 1 &&
					respawn.spawnerDataId == initialSpawn.spawnerDataId,
			"Respawn did not advance both Entity identity and SpawnGeneration exactly once.");
	}

	void TestMonsterCollisionKeepsUnplacedSpawnPending()
	{
		const SMapDefinition definition = MakeDefinition(30, 30, 10);
		std::unique_ptr<FMapInstance> map = CreateMap(640, definition);
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'020, 1'020, {1.0f, 1.0f}, {3.1f, 3.1f}, 2, 2, 1);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(13, spawners, error), error);

		const SMapTickResult firstTick = map->Tick();
		Require(firstTick.result == EMapTickResult::Completed && firstTick.spawnResults.size() == 1 && map->GetMonsterCount() == 1,
			"Collision-constrained SpawnArea did not commit its one valid Monster.");
		for (std::uint32_t attempt = 0; attempt < 4; ++attempt)
		{
			const SMapTickResult retryTick = map->Tick();
			Require(retryTick.result == EMapTickResult::Completed && retryTick.spawnResults.empty() && map->GetMonsterCount() == 1,
				"An unplaceable Monster was lost, over-counted, or committed through a collision.");
		}
	}

	void TestMonsterSpawnFailureRollsBackMovementCommit()
	{
		SMapDefinition definition = MakeDefinition(30, 10, 10);
		definition.maxAcceptedPositionError = 20.0f;
		std::unique_ptr<FMapInstance> map = CreateMap(645, definition);
		AddPlayer(*map, 1, {9.0f, 1.0f});
		const SMonsterSpawnerRuntimeDefinition spawner =
			MakeMonsterSpawner(definition.mapDataId, 3'030, 1'030, {12.0f, 2.0f}, {28.0f, 8.0f}, 1, 1, 2);
		const std::vector<SMonsterSpawnerRuntimeDefinition> spawners{spawner};
		std::string error;
		Require(map->ConfigureMonsterSpawning(19, spawners, error), error);

		const FPlayerEntity* const playerBefore = map->FindPlayer(1);
		Require(playerBefore != nullptr, "Spawn rollback test Player was not registered.");
		const std::uint64_t stateHashBefore = map->GetStateHash();
		const SVector2 positionBefore = playerBefore->GetPosition();
		const SVector2 directionBefore = playerBefore->GetDirection();
		const FSectorId sectorBefore = playerBefore->GetSectorId();
		const FMoveSequence sequenceBefore = playerBefore->GetLastMoveSequence();
		const EMoveState moveStateBefore = playerBefore->GetMoveState();

		Require(map->QueueMove({1, 1, EMoveState::Start, {11.0f, 1.0f}, {1.0f, 0.0f}}), "Spawn rollback test Move was rejected.");
		FMapInstanceTestAccess::InjectNextMonsterSpawnCommitFailure(*map);
		const SMapTickResult failedTick = map->Tick();
		Require(failedTick.result == EMapTickResult::Failed && failedTick.failureReason == "Injected Monster Spawn Commit failure." &&
					failedTick.moveResults.empty() && failedTick.spawnResults.empty(),
			"Injected Spawn failure did not fail after removing rolled-back Commit outputs.");

		const FPlayerEntity* const playerAfter = map->FindPlayer(1);
		Require(playerAfter != nullptr && playerAfter->GetPosition() == positionBefore && playerAfter->GetDirection() == directionBefore &&
					playerAfter->GetSectorId() == sectorBefore && playerAfter->GetLastMoveSequence() == sequenceBefore &&
					playerAfter->GetMoveState() == moveStateBefore,
			"Spawn Commit failure did not restore the complete Player movement state.");
		Require(map->GetSectorGrid().GetEntitySectorId(1) == std::optional<FSectorId>(sectorBefore) &&
					map->GetSectorGrid().ContainsEntity(sectorBefore, 1),
			"Spawn Commit failure did not reverse the Player Sector transfer.");
		Require(map->GetMonsterCount() == 0 && map->GetTickIndex() == 0 && map->GetStateHash() == stateHashBefore,
			"Spawn Commit failure changed Monster state, committed Tick, or the complete Map state hash.");

		const SMapTickResult recoveredTick = map->Tick();
		Require(recoveredTick.result == EMapTickResult::Completed && recoveredTick.tickIndex == 1 &&
					recoveredTick.spawnResults.size() == 1 && map->GetMonsterCount() == 1,
			"Spawn request was consumed by the failed logical Tick or failed to recover on retry.");
	}

	std::uint64_t RunDeterministicScenario()
	{
		SMapDefinition definition = MakeDefinition(100, 100, 10);
		definition.maxAcceptedPositionError = 8.0f;
		std::unique_ptr<FMapInstance> map = CreateMap(300, definition);
		for (FEntityId entityId = 1; entityId <= 4; ++entityId)
		{
			AddPlayer(*map, entityId, {static_cast<float>(entityId * 10 + 1), static_cast<float>(entityId * 10 + 1)});
		}

		for (std::uint32_t tickIndex = 1; tickIndex <= 50; ++tickIndex)
		{
			for (FEntityId entityId = 1; entityId <= 4; ++entityId)
			{
				const FPlayerEntity* const player = map->FindPlayer(entityId);
				const float xOffset = ((tickIndex + entityId) % 2 == 0) ? 1.25f : -0.75f;
				const float yOffset = ((tickIndex + entityId) % 3 == 0) ? 0.5f : -0.25f;
				Require(map->QueueMove({entityId,
							tickIndex,
							EMoveState::Sync,
							{player->GetPosition().x + xOffset, player->GetPosition().y + yOffset},
							{xOffset, yOffset}}),
					"Deterministic scenario input was rejected.");
			}
			const SMapTickResult tick = map->Tick();
			Require(tick.result == EMapTickResult::Completed, tick.failureReason);
		}
		return map->GetStateHash();
	}

	void TestDeterministicStateHash()
	{
		const std::uint64_t firstHash = RunDeterministicScenario();
		const std::uint64_t secondHash = RunDeterministicScenario();
		Require(firstHash == secondHash, "Identical input produced a different World state hash.");
	}

	void TestTaskGraphSelectionFailsExplicitly()
	{
		SMapDefinition definition = MakeDefinition();
		definition.sectorExecutionMode = ESectorExecutionMode::TaskGraph;
		FMapInstanceFactory factory;
		EMapCreateResult result = EMapCreateResult::Success;
		std::string error;
		Require(factory.Create(400, definition, result, error) == nullptr, "Unimplemented TaskGraph mode silently created a Map.");
		Require(result == EMapCreateResult::UnsupportedExecutionMode && error.find("not implemented") != std::string::npos,
			"TaskGraph failure was not reported explicitly.");
	}

	void TestAsyncTickPendingAndInputIsolation()
	{
		SMapDefinition definition = MakeDefinition(30, 10, 10);
		definition.maxAcceptedPositionError = 30.0f;
		FPendingSectorExecutor* executor = nullptr;
		std::unique_ptr<FMapInstance> map = CreatePendingMap(500, definition, executor);
		AddPlayer(*map, 1, {1.0f, 1.0f});

		Require(map->QueueMove({1, 1, EMoveState::Start, {11.0f, 1.0f}, {1.0f, 0.0f}}), "First async Move was rejected.");
		const SMapTickResult firstStart = map->Tick();
		Require(firstStart.result == EMapTickResult::Pending && firstStart.executionStarted, "Async Tick did not enter Pending state.");
		Require(firstStart.consumedMoveRequests == std::vector<SMoveRequestIdentity>({{1, 1}}),
			"Async Tick did not report its consumed Move request.");
		Require(map->GetTickExecutionState() == EMapTickExecutionState::Executing && executor->GetExecuteCount() == 1,
			"Async Tick execution state is incorrect.");
		Require(map->GetTickIndex() == 0 && map->FindPlayer(1)->GetPosition() == SVector2{1.0f, 1.0f},
			"Pending Tick changed committed World state.");

		const SMapTickResult repeatedPoll = map->Tick();
		Require(repeatedPoll.result == EMapTickResult::Pending && !repeatedPoll.executionStarted && executor->GetExecuteCount() == 1,
			"Pending Tick started the executor more than once.");
		Require(repeatedPoll.tickIndex == firstStart.tickIndex && repeatedPoll.tickGeneration == firstStart.tickGeneration,
			"Pending Tick identity changed while waiting for completion.");

		Require(map->QueueMove({1, 2, EMoveState::Sync, {21.0f, 1.0f}, {1.0f, 0.0f}}),
			"Next-Tick Move was rejected while the current Tick was Pending.");
		Require(map->GetPendingMoveCount() == 1, "Next-Tick input was consumed by the active Tick.");

		std::vector<SVisibilityEvent> events;
		std::string error;
		Require(!map->AddPlayer(2, 102, {2.0f, 1.0f}, {1.0f, 0.0f}, events, error), "Player registration mutated an active async Tick.");
		Require(!map->RemovePlayer(1, events, error), "Player removal mutated an active async Tick.");

		SMapTickExecutionCompletion staleCompletion = executor->MakeSuccessCompletion();
		++staleCompletion.ticket.generation;
		Require(map->CompleteTickExecution(std::move(staleCompletion)) == EMapTickCompletionResult::StaleTicket,
			"Stale async completion was accepted.");
		Require(map->GetTickExecutionState() == EMapTickExecutionState::Executing, "Stale completion changed the active Tick state.");

		const SMapTickExecutionCompletion firstCompletion = executor->MakeSuccessCompletion();
		Require(
			map->CompleteTickExecution(firstCompletion) == EMapTickCompletionResult::Accepted, "Matching async completion was rejected.");
		Require(map->CompleteTickExecution(firstCompletion) == EMapTickCompletionResult::DuplicateCompletion,
			"Duplicate async completion was not detected.");
		Require(map->GetTickExecutionState() == EMapTickExecutionState::ReadyToCommit &&
					map->FindPlayer(1)->GetPosition() == SVector2{1.0f, 1.0f},
			"Completion delivery committed World state outside the Map owner Tick.");

		const SMapTickResult firstCommit = map->Tick();
		Require(firstCommit.result == EMapTickResult::Completed && map->GetTickExecutionState() == EMapTickExecutionState::Idle,
			firstCommit.failureReason);
		Require(map->GetTickIndex() == 1 && map->FindPlayer(1)->GetPosition() == SVector2{11.0f, 1.0f} &&
					map->GetSectorGrid().GetEntitySectorId(1) == std::optional<FSectorId>(1),
			"Owner Tick did not commit the completed async result.");
		Require(map->GetPendingMoveCount() == 1, "Committing the first Tick consumed the next Tick input.");

		const SMapTickResult secondStart = map->Tick();
		Require(secondStart.result == EMapTickResult::Pending && secondStart.executionStarted && executor->GetExecuteCount() == 2,
			"Next async Tick did not start after Commit.");
		Require(std::ranges::equal(executor->GetCapturedMoveRequests(), secondStart.consumedMoveRequests),
			"Next async Tick did not capture the isolated Move request.");
		Require(secondStart.consumedMoveRequests == std::vector<SMoveRequestIdentity>({{1, 2}}),
			"Next async Tick captured the wrong Move sequence.");
		Require(map->CompleteTickExecution(firstCompletion) == EMapTickCompletionResult::StaleTicket,
			"A late completion from the previous generation affected the next Tick.");

		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Second async completion was rejected.");
		const SMapTickResult secondCommit = map->Tick();
		Require(secondCommit.result == EMapTickResult::Completed && map->GetTickIndex() == 2, secondCommit.failureReason);
		Require(map->FindPlayer(1)->GetPosition() == SVector2{21.0f, 1.0f}, "Second async Move was not committed.");
	}

	void TestAsyncTickFailureRecovery()
	{
		FPendingSectorExecutor* executor = nullptr;
		std::unique_ptr<FMapInstance> map = CreatePendingMap(501, MakeDefinition(30, 10, 10), executor);
		AddPlayer(*map, 1, {1.0f, 1.0f});
		Require(map->QueueMove({1, 1, EMoveState::Sync, {5.0f, 1.0f}, {1.0f, 0.0f}}), "Failure test Move was rejected.");
		const SMapTickResult started = map->Tick();
		Require(started.result == EMapTickResult::Pending, "Failure test Tick did not start.");

		Require(map->CompleteTickExecution(executor->MakeFailureCompletion("injected async failure")) == EMapTickCompletionResult::Accepted,
			"Async failure completion was rejected.");
		Require(map->GetTickExecutionState() == EMapTickExecutionState::Failed && map->FindPlayer(1)->GetPosition() == SVector2{1.0f, 1.0f},
			"Async failure partially committed World state.");
		const SMapTickResult failed = map->Tick();
		Require(failed.result == EMapTickResult::Failed && failed.failureReason == "injected async failure",
			"Async failure reason was not propagated.");
		Require(map->GetTickExecutionState() == EMapTickExecutionState::Idle && map->GetTickIndex() == 0,
			"Failed Tick did not return the Map to Idle without advancing committed Tick.");

		Require(map->QueueMove({1, 2, EMoveState::Sync, {6.0f, 1.0f}, {1.0f, 0.0f}}), "Map did not accept input after async failure.");
		const SMapTickResult invalidOutputStart = map->Tick();
		Require(invalidOutputStart.result == EMapTickResult::Pending, "Invalid-output Tick did not start.");
		SMapTickExecutionCompletion invalidOutput = executor->MakeSuccessCompletion();
		invalidOutput.taskOutputs.pop_back();
		Require(map->CompleteTickExecution(std::move(invalidOutput)) == EMapTickCompletionResult::Accepted,
			"Invalid completion was not consumed as a failed Tick.");
		const SMapTickResult invalidOutputResult = map->Tick();
		Require(invalidOutputResult.result == EMapTickResult::Failed &&
					invalidOutputResult.failureReason.find("output count") != std::string::npos,
			"Invalid async output did not fail before Commit.");
		Require(map->FindPlayer(1)->GetPosition() == SVector2{1.0f, 1.0f} && map->GetTickIndex() == 0,
			"Invalid async output partially changed World state.");

		Require(map->QueueMove({1, 3, EMoveState::Sync, {7.0f, 1.0f}, {1.0f, 0.0f}}), "Map did not recover after invalid async output.");
		Require(map->Tick().result == EMapTickResult::Pending, "Recovery Tick did not start.");
		Require(map->CompleteTickExecution(executor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Recovery completion was rejected.");
		const SMapTickResult recovered = map->Tick();
		Require(recovered.result == EMapTickResult::Completed && map->FindPlayer(1)->GetPosition() == SVector2{7.0f, 1.0f},
			recovered.failureReason);
	}

	void TestAsyncTickMapIncarnationGuard()
	{
		FPendingSectorExecutor* firstExecutor = nullptr;
		std::unique_ptr<FMapInstance> firstMap = CreatePendingMap(502, MakeDefinition(30, 10, 10), firstExecutor);
		AddPlayer(*firstMap, 1, {1.0f, 1.0f});
		Require(firstMap->Tick().result == EMapTickResult::Pending, "First Map incarnation did not start.");
		const SMapTickExecutionCompletion oldCompletion = firstExecutor->MakeSuccessCompletion();
		firstMap.reset();

		FPendingSectorExecutor* secondExecutor = nullptr;
		std::unique_ptr<FMapInstance> secondMap = CreatePendingMap(502, MakeDefinition(30, 10, 10), secondExecutor);
		AddPlayer(*secondMap, 1, {1.0f, 1.0f});
		Require(secondMap->Tick().result == EMapTickResult::Pending, "Replacement Map incarnation did not start.");
		Require(secondMap->GetActiveTickTicket().mapIncarnation != oldCompletion.ticket.mapIncarnation,
			"Replacement Map reused the previous incarnation.");
		Require(secondMap->CompleteTickExecution(oldCompletion) == EMapTickCompletionResult::StaleTicket,
			"Completion from a destroyed Map incarnation was accepted.");
		Require(secondMap->CompleteTickExecution(secondExecutor->MakeSuccessCompletion()) == EMapTickCompletionResult::Accepted,
			"Replacement Map completion was rejected.");
		Require(secondMap->Tick().result == EMapTickResult::Completed,
			"Replacement Map did not complete after rejecting the stale incarnation.");
	}
}

int main()
{
	FTestRunner runner;
	runner.Run("Map definition and Sector coordinates", TestMapDefinitionAndSectorCoordinates);
	runner.Run("Sector Tick Plan four-Wave partition", TestSectorTickPlanWavePartition);
	runner.Run("Entity registry ownership", TestEntityRegistry);
	runner.Run("Actor and Monster registry ownership", TestActorAndMonsterRegistry);
	runner.Run("Monster as Player visibility subject", TestMonsterVisibilitySubject);
	runner.Run("Player runtime Snapshot revision and HP-MP clamp", TestPlayerRuntimeSnapshotReplacement);
	runner.Run("Move sequence and Tick input isolation", TestInputSequenceAndTickIsolation);
	runner.Run("Serial task stable order and read-only execution", TestSerialTaskOrderAndReadOnlyExecution);
	runner.Run("Movement correction and deferred Sector transfer", TestMovementCorrectionAndDeferredTransfer);
	runner.Run("Map visibility Spawn/Despawn/Move", TestVisibilityEvents);
	runner.Run("Multiple MapInstance isolation", TestMultipleMapIsolation);
	runner.Run("Monster initial spawn and capacity", TestMonsterInitialSpawnAndCapacity);
	runner.Run("Monster deterministic spawn and Map isolation", TestMonsterSpawnDeterminismAndMapIsolation);
	runner.Run("Monster Spawner stable Commit order", TestMonsterSpawnerStableOrder);
	runner.Run("Monster AI Spawn boundary, target tie, and AttackRange", TestMonsterAiSpawnBoundaryNearestTieAndAttackRange);
	runner.Run("Monster Aggro type and Spawn-origin Leash return", TestMonsterAggroTypeAndLeashReturn);
	runner.Run("Monster basic attack damage, Cooldown, and visibility", TestMonsterBasicAttackDamageCooldownAndVisibility);
	runner.Run("Multiple Monster attacks use stable Commit order", TestMultipleMonsterAttacksUseStableCommitOrder);
	runner.Run("Monster attack rollback on downstream failure", TestMonsterAttackRollsBackOnDownstreamFailure);
	runner.Run("Monster attack waits for async Owner Commit", TestMonsterAttackWaitsForAsyncOwnerCommit);
	runner.Run("Player random first Spawn determinism and collision", TestPlayerRandomFirstSpawnIsDeterministicAndCollisionFree);
	runner.Run("Player Death rollback and exact random Respawn", TestPlayerDeathRollbackAndExactRandomRespawn);
	runner.Run("Player Basic Attack validation, lethal ordering, and Monster Respawn", TestPlayerBasicAttackLifecycleAndValidation);
	runner.Run("Player Attack dead state and atomic rollback", TestPlayerAttackDeadAttackerAndAtomicRollback);
	runner.Run("Monster AI deferred Sector transfer", TestMonsterAiDeferredSectorTransfer);
	runner.Run("Monster AI waits for async Owner Commit", TestMonsterAiWaitsForAsyncCommit);
	runner.Run("Monster AI rollback on downstream Spawn failure", TestMonsterAiRollsBackOnDownstreamSpawnFailure);
	runner.Run("Monster spawn waits for async Commit", TestMonsterSpawnWaitsForAsyncCommit);
	runner.Run("Monster removal and exact respawn delay", TestMonsterRemovalAndExactRespawnDelay);
	runner.Run("Monster collision keeps unplaced spawn pending", TestMonsterCollisionKeepsUnplacedSpawnPending);
	runner.Run("Monster Spawn failure rolls back movement Commit", TestMonsterSpawnFailureRollsBackMovementCommit);
	runner.Run("Deterministic state hash", TestDeterministicStateHash);
	runner.Run("TaskGraph selection fails explicitly", TestTaskGraphSelectionFailsExplicitly);
	runner.Run("Async Tick Pending state and input isolation", TestAsyncTickPendingAndInputIsolation);
	runner.Run("Async Tick failure and recovery", TestAsyncTickFailureRecovery);
	runner.Run("Async Tick Map incarnation guard", TestAsyncTickMapIncarnationGuard);
	return runner.Finish();
}
