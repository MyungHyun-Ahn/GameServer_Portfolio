#include "WorldServerPch.h"

#include "WorldServer/Application/FWorldServerBootstrap.h"

#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "Connector/Redis/FRedisChatTicketStore.h"
#include "Foundation/Logging/FCompositeLogger.h"
#include "Foundation/Logging/FConsoleLogger.h"
#include "Foundation/Logging/FFileLogger.h"
#include "GameData/Common/TGameDataRow.h"
#include "Generated/GameData/Cpp/CombatFormulaPolicy/CombatFormulaPolicyData.g.h"
#include "Generated/GameData/Cpp/Common/GameDataEnums.g.h"
#include "Generated/GameData/Cpp/Character/CharacterData.g.h"
#include "Generated/GameData/Cpp/CharacterLevel/CharacterLevelData.g.h"
#include "Generated/GameData/Cpp/Item/ItemData.g.h"
#include "Generated/GameData/Cpp/Monster/MonsterData.g.h"
#include "Generated/GameData/Cpp/MonsterSpawner/MonsterSpawnerData.g.h"
#include "Generated/GameData/Cpp/SpawnArea/SpawnAreaData.g.h"
#include "Generated/GameData/Cpp/StatConversion/StatConversionData.g.h"
#include "GameData/Character/FCharacterDataTable.h"
#include "GameData/CharacterLevel/FCharacterLevelDataTable.h"
#include "GameData/CombatFormulaPolicy/FCombatFormulaPolicyTable.h"
#include "GameData/Item/FItemDataTable.h"
#include "Generated/GameData/Cpp/Map/MapData.g.h"
#include "GameData/Map/FMapDataTable.h"
#include "GameData/Monster/FMonsterDataTable.h"
#include "GameData/MonsterSpawner/FMonsterSpawnerDataTable.h"
#include "GameData/SpawnArea/FSpawnAreaDataTable.h"
#include "GameData/StatConversion/FStatConversionTable.h"
#include "Generated/Config/WorldServer/WorldServerConfig.h"
#include "NetworkLib/Crypto/FDefaultPacketCipher.h"
#include "NetworkLib/Packet/Framing/FDefaultPacketFramer.h"
#include "NetworkLib/Servers/Core/FServerFactory.h"
#include "NetworkLib/Servers/IApplicationHandler.h"
#include "WorldServer/Contents/FMapRouterContent.h"
#include "WorldServer/Contents/Map/FMapContentShard.h"
#include "WorldServer/Contents/Map/Sector/FSectorExecutorContent.h"
#include "WorldServer/Contents/Map/Sector/FTaskGraphSectorExecutionService.h"
#include "WorldServer/Contents/Session/FWorldSession.h"
#include "WorldServer/Contents/Session/FWorldSessionRegistry.h"
#include "WorldServer/Domain/FPlayerStatCalculator.h"

namespace
{
	constexpr std::uint64_t kDefaultMapMailboxCapacity = 65536;
	constexpr std::uint64_t kDefaultSectorExecutorMailboxCapacity = 65536;
	constexpr std::uint8_t kRpcRandomKey = 0x51;

	struct SCommandLineOptions final
	{
		std::optional<std::filesystem::path> configPath;
		std::uint32_t runSeconds = 0;
	};

	struct SWorldRuntimeConfig final
	{
		std::uint32_t mapContentShardCount = 1;
		std::uint32_t mapTickFps = 20;
		std::uint32_t sectorExecutorInstanceCount = 1;
		std::uint32_t sectorTaskPumpBatchSize = 1;
		float movementCorrectionTolerance = 64.0f;
		std::filesystem::path gameDataDirectory;
		bool headless = false;
		bool authEnabled = false;
		Connector::SRedisLoginTicketStoreConfig loginTicketStoreConfig;
		bool cacheEnabled = false;
		std::string cacheHost;
		std::uint16_t cachePort = 0;
		std::uint8_t cacheRpcPacketKey = 0;
		RpcLib::Protocol::FRpcServerInstanceId rpcServerInstanceId = 0;
		RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId = 0;
		std::chrono::milliseconds cacheRpcTimeout{3000};
		std::chrono::milliseconds cacheReconnectInterval{1000};
	};

	std::filesystem::path GetExecutableDirectory()
	{
		std::array<char, MAX_PATH> modulePath{};
		const DWORD pathLength = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (pathLength == 0 || pathLength >= modulePath.size())
		{
			return std::filesystem::current_path();
		}

		return std::filesystem::path(modulePath.data()).parent_path();
	}

	bool TryParseUInt32(
		const char* text,
		std::uint32_t& outValue) noexcept
	{
		if (text == nullptr || *text == '\0')
		{
			return false;
		}

		char* end = nullptr;
		const unsigned long parsed = std::strtoul(text, &end, 10);
		if (end == text || *end != '\0' || parsed > std::numeric_limits<std::uint32_t>::max())
		{
			return false;
		}

		outValue = static_cast<std::uint32_t>(parsed);
		return true;
	}

	bool TryParseCommandLine(
		const int argc,
		char* argv[],
		SCommandLineOptions& outOptions)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument(argv[index]);
			if (argument == "--config" && index + 1 < argc)
			{
				outOptions.configPath = std::filesystem::path(argv[++index]);
				continue;
			}

			if (argument == "--run-seconds" && index + 1 < argc)
			{
				if (!TryParseUInt32(argv[++index], outOptions.runSeconds))
				{
					return false;
				}
				continue;
			}

			return false;
		}

		return true;
	}

	std::filesystem::path ResolveDefaultConfigPath(
		const std::filesystem::path& executableDirectory)
	{
		return executableDirectory / "Config" / "Server" / "WorldServer.yaml";
	}

	std::filesystem::path ResolveGameDataDirectory(
		const std::filesystem::path& executableDirectory,
		const std::string& configuredDirectory)
	{
		const std::filesystem::path directory(configuredDirectory);
		if (directory.is_absolute())
		{
			return directory;
		}

		return executableDirectory / "Config" / directory;
	}

	std::filesystem::path ResolveLogDirectory(
		const std::filesystem::path& executableDirectory,
		const std::string& configuredDirectory)
	{
		if (configuredDirectory.empty())
		{
			return executableDirectory / "logs";
		}

		const std::filesystem::path directory(configuredDirectory);
		return directory.is_absolute() ? directory : executableDirectory / directory;
	}

	NetworkLib::Core::EBackendKind ToBackendKind(
		const Generated::Config::WorldServer::EBackend backend) noexcept
	{
		switch (backend)
		{
			case Generated::Config::WorldServer::EBackend::Iocp:
				return NetworkLib::Core::EBackendKind::Iocp;
			case Generated::Config::WorldServer::EBackend::Rio:
				return NetworkLib::Core::EBackendKind::Rio;
		}

		return NetworkLib::Core::EBackendKind::Iocp;
	}

	NetworkLib::Core::ERioSendDispatchMode ToRioSendDispatchMode(
		const Generated::Config::WorldServer::ERioSendDispatchMode mode) noexcept
	{
		switch (mode)
		{
			case Generated::Config::WorldServer::ERioSendDispatchMode::Direct:
				return NetworkLib::Core::ERioSendDispatchMode::Direct;
			case Generated::Config::WorldServer::ERioSendDispatchMode::OwnerThread:
				return NetworkLib::Core::ERioSendDispatchMode::OwnerThread;
		}

		return NetworkLib::Core::ERioSendDispatchMode::OwnerThread;
	}

	Foundation::ELogLevel ToLogLevel(
		const Generated::Config::WorldServer::ELogMinimumLevel level) noexcept
	{
		switch (level)
		{
			case Generated::Config::WorldServer::ELogMinimumLevel::Debug:
				return Foundation::ELogLevel::Debug;
			case Generated::Config::WorldServer::ELogMinimumLevel::Info:
				return Foundation::ELogLevel::Info;
			case Generated::Config::WorldServer::ELogMinimumLevel::Warn:
				return Foundation::ELogLevel::Warn;
			case Generated::Config::WorldServer::ELogMinimumLevel::Error:
				return Foundation::ELogLevel::Error;
		}

		return Foundation::ELogLevel::Info;
	}

	bool ApplyConfig(
		const Generated::Config::WorldServer::FWorldServerConfigDocument& document,
		const std::filesystem::path& executableDirectory,
		NetworkLib::Core::SServerConfig& outServerConfig,
		ContentsRuntime::Core::SContentRuntimeConfig& outContentRuntimeConfig,
		SWorldRuntimeConfig& outWorldRuntimeConfig,
		std::uint32_t& outPacketKey,
		std::string& outError)
	{
		const auto& config = document.WorldServer;
		if (config.PacketKey > 0xFFu)
		{
			outError = "WorldServer.PacketKey must be in range 0..255.";
			return false;
		}
		if (config.WorkerThreadCount <= 0 || config.ContentsWorkerThreadCount <= 0 || config.SectorExecutorInstanceCount <= 0 ||
			config.MapContentShardCount <= 0)
		{
			outError = "WorldServer worker and shard counts must be positive.";
			return false;
		}
		if (config.MapContentShardCount > 256)
		{
			outError = "WorldServer.MapContentShardCount must not exceed 256.";
			return false;
		}
		if (config.SectorExecutorInstanceCount > 256 || config.SectorTaskPumpBatchSize == 0 || config.SectorTaskPumpBatchSize > 4096)
		{
			outError = "WorldServer SectorExecutorInstanceCount must be in range 1..256 and SectorTaskPumpBatchSize in range 1..4096.";
			return false;
		}
		if (config.MapTickFps == 0 || config.MapTickFps > 1000)
		{
			outError = "WorldServer.MapTickFps must be in range 1..1000.";
			return false;
		}
		if (!std::isfinite(config.MovementCorrectionTolerance) || config.MovementCorrectionTolerance < 0.0f)
		{
			outError = "WorldServer.MovementCorrectionTolerance must be finite and non-negative.";
			return false;
		}
		if (config.GameDataDirectory.empty())
		{
			outError = "WorldServer.GameDataDirectory must not be empty.";
			return false;
		}
		if (config.CacheRpcPacketKey > 0xFFu)
		{
			outError = "WorldServer.CacheRpcPacketKey must be in range 0..255.";
			return false;
		}
		const bool authEnabled = config.AuthMode == Generated::Config::WorldServer::EAuthMode::Redis;
		if (authEnabled && (config.LoginRedisHost.empty() || config.LoginRedisPort == 0 || config.LoginRedisDatabase < 0 ||
							   config.LoginRedisConnectTimeoutMilliseconds == 0 || config.WorldTicketKeyPrefix.empty() ||
							   config.ActiveLoginKeyPrefix.empty() || config.RpcServerInstanceId == 0))
		{
			outError = "WorldServer Redis authentication configuration is invalid.";
			return false;
		}
		if (config.CacheEnabled &&
			(config.CacheHost.empty() || config.CachePort == 0 || config.RpcServerInstanceId == 0 || config.CacheServerInstanceId == 0 ||
				config.CacheRpcTimeoutMilliseconds == 0 || config.CacheReconnectMilliseconds == 0))
		{
			outError = "WorldServer Cache RPC configuration is invalid.";
			return false;
		}

		outServerConfig.backendKind = ToBackendKind(config.Backend);
		outServerConfig.rioSendDispatchMode = ToRioSendDispatchMode(config.RioSendDispatchMode);
		outServerConfig.bindIp = config.BindIp;
		outServerConfig.port = config.Port;
		outServerConfig.workerThreadCount = config.WorkerThreadCount;
		outServerConfig.maxSessionCount = config.MaxSessionCount;
		outServerConfig.recvBufferSize = config.RecvBufferSize;
		outServerConfig.socketSendBufferBytes = config.SocketSendBufferBytes;
		outServerConfig.rioSendRingSizeBytes = std::max<std::uint32_t>(
			static_cast<std::uint32_t>(NetworkLib::Packet::Framing::kMaxFramedPacketSizeBytes), config.RioSendRingSizeBytes);
		outServerConfig.logConfig.minimumLevel = ToLogLevel(config.LogMinimumLevel);
		outServerConfig.logConfig.outputDirectory = ResolveLogDirectory(executableDirectory, config.LogOutputDirectory).string();
		outServerConfig.logConfig.consoleEnabled = config.LogConsoleEnabled;
		outServerConfig.logConfig.fileEnabled = config.LogFileEnabled;
		outServerConfig.logConfig.includeThreadId = config.LogIncludeThreadId;

		outContentRuntimeConfig.workerThreadCount = static_cast<std::uint32_t>(config.ContentsWorkerThreadCount);
		outWorldRuntimeConfig.mapContentShardCount = static_cast<std::uint32_t>(config.MapContentShardCount);
		outWorldRuntimeConfig.mapTickFps = config.MapTickFps;
		outWorldRuntimeConfig.sectorExecutorInstanceCount = static_cast<std::uint32_t>(config.SectorExecutorInstanceCount);
		outWorldRuntimeConfig.sectorTaskPumpBatchSize = config.SectorTaskPumpBatchSize;
		outWorldRuntimeConfig.movementCorrectionTolerance = config.MovementCorrectionTolerance;
		outWorldRuntimeConfig.gameDataDirectory = ResolveGameDataDirectory(executableDirectory, config.GameDataDirectory);
		outWorldRuntimeConfig.headless = document.Debug.Headless;
		outWorldRuntimeConfig.authEnabled = authEnabled;
		outWorldRuntimeConfig.loginTicketStoreConfig.connection.host = config.LoginRedisHost;
		outWorldRuntimeConfig.loginTicketStoreConfig.connection.port = config.LoginRedisPort;
		outWorldRuntimeConfig.loginTicketStoreConfig.connection.password = config.LoginRedisPassword;
		outWorldRuntimeConfig.loginTicketStoreConfig.connection.database = config.LoginRedisDatabase;
		outWorldRuntimeConfig.loginTicketStoreConfig.connection.connectTimeoutMs = config.LoginRedisConnectTimeoutMilliseconds;
		outWorldRuntimeConfig.loginTicketStoreConfig.keyPrefix = config.WorldTicketKeyPrefix;
		outWorldRuntimeConfig.loginTicketStoreConfig.activeLoginKeyPrefix = config.ActiveLoginKeyPrefix;
		outWorldRuntimeConfig.cacheEnabled = config.CacheEnabled;
		outWorldRuntimeConfig.cacheHost = config.CacheHost;
		outWorldRuntimeConfig.cachePort = config.CachePort;
		outWorldRuntimeConfig.cacheRpcPacketKey = static_cast<std::uint8_t>(config.CacheRpcPacketKey);
		outWorldRuntimeConfig.rpcServerInstanceId = config.RpcServerInstanceId;
		outWorldRuntimeConfig.cacheServerInstanceId = config.CacheServerInstanceId;
		outWorldRuntimeConfig.cacheRpcTimeout = std::chrono::milliseconds(config.CacheRpcTimeoutMilliseconds);
		outWorldRuntimeConfig.cacheReconnectInterval = std::chrono::milliseconds(config.CacheReconnectMilliseconds);
		outPacketKey = config.PacketKey;
		outError.clear();
		return true;
	}

	bool BuildBootMapDefinitions(
		const GameData::Map::FMapDataTable& mapDataTable,
		const GameData::CombatFormulaPolicy::SCombatFormulaPolicyData& combatFormulaPolicy,
		const GameData::Monster::FMonsterDataTable& monsterDataTable,
		const GameData::SpawnArea::FSpawnAreaDataTable& spawnAreaDataTable,
		const GameData::MonsterSpawner::FMonsterSpawnerDataTable& monsterSpawnerDataTable,
		const float movementCorrectionTolerance,
		const std::uint32_t mapTickFps,
		std::vector<WorldServer::Contents::SBootMapDefinition>& outDefinitions,
		std::string& outError)
	{
		if (mapTickFps == 0)
		{
			outError = "Map Tick FPS must be greater than zero while building Monster spawn definitions.";
			return false;
		}

		const std::vector<const GameData::Map::SMapData*> rows = mapDataTable.GetAll();
		outDefinitions.clear();
		outDefinitions.reserve(rows.size());
		WorldCore::FMapInstanceId nextMapInstanceId = 1;

		for (const GameData::Map::SMapData* const row : rows)
		{
			if (row == nullptr || nextMapInstanceId == WorldCore::kInvalidMapInstanceId)
			{
				outError = "map data contains an invalid row or MapInstanceId allocation overflowed.";
				return false;
			}

			WorldServer::Contents::SBootMapDefinition bootMap{};
			bootMap.mapDataId = row->mapDataId;
			bootMap.mapInstanceId = nextMapInstanceId++;
			bootMap.definition.mapDataId = row->mapDataId;
			bootMap.definition.worldWidth = row->worldWidth;
			bootMap.definition.worldHeight = row->worldHeight;
			bootMap.definition.sectorSize = row->sectorSize;
			bootMap.definition.visibilitySectorRadius = row->aoiSectorRadius;
			bootMap.definition.spawnPosition = {row->spawnX, row->spawnY};
			bootMap.definition.maxAcceptedPositionError = movementCorrectionTolerance;
			bootMap.definition.tickRateHz = mapTickFps;
			bootMap.definition.combatPolicy.minimumDamage = combatFormulaPolicy.minimumDamage;
			bootMap.definition.combatPolicy.playerBasicAttackRange = combatFormulaPolicy.playerBasicAttackRange;
			bootMap.definition.combatPolicy.playerBasicAttackCooldownMilliseconds =
				combatFormulaPolicy.playerBasicAttackCooldownMilliseconds;

			const std::vector<const GameData::SpawnArea::SSpawnAreaData*> mapSpawnAreas = spawnAreaDataTable.FindByMap(row->mapDataId);
			if (mapSpawnAreas.empty() || mapSpawnAreas.front() == nullptr)
			{
				outError = std::format("MapDataId {} requires at least one SpawnArea for Player entry and Respawn.", row->mapDataId);
				return false;
			}
			const GameData::SpawnArea::SSpawnAreaData& playerSpawnArea = *mapSpawnAreas.front();
			bootMap.definition.playerSpawnAreaMinimum = {playerSpawnArea.minX, playerSpawnArea.minY};
			bootMap.definition.playerSpawnAreaMaximum = {playerSpawnArea.maxX, playerSpawnArea.maxY};

			const std::uint64_t playerRespawnMilliseconds = combatFormulaPolicy.playerRespawnDelayMilliseconds;
			if (playerRespawnMilliseconds > (std::numeric_limits<std::uint64_t>::max() - 999ull) / static_cast<std::uint64_t>(mapTickFps))
			{
				outError = std::format("MapDataId {} Player Respawn delay overflowed the Tick conversion.", row->mapDataId);
				return false;
			}
			bootMap.definition.playerRespawnDelayTicks =
				std::max<std::uint64_t>(1ull, (playerRespawnMilliseconds * static_cast<std::uint64_t>(mapTickFps) + 999ull) / 1000ull);

			switch (row->sectorExecutionMode)
			{
				case GameData::Common::ESectorExecutionMode::Serial:
					bootMap.definition.sectorExecutionMode = WorldCore::ESectorExecutionMode::Serial;
					break;
				case GameData::Common::ESectorExecutionMode::TaskGraph:
					bootMap.definition.sectorExecutionMode = WorldCore::ESectorExecutionMode::TaskGraph;
					break;
				default:
					outError = std::format("MapDataId {} has an unknown SectorExecutionMode.", row->mapDataId);
					return false;
			}

			std::string definitionError;
			if (!WorldCore::IsValidMapDefinition(bootMap.definition, definitionError))
			{
				outError = std::format("MapDataId {} could not be converted to WorldCore: {}", row->mapDataId, definitionError);
				return false;
			}

			for (const GameData::MonsterSpawner::SMonsterSpawnerData* const spawner : monsterSpawnerDataTable.FindByMap(row->mapDataId))
			{
				if (spawner == nullptr)
				{
					outError = std::format("MapDataId {} contains a null MonsterSpawner row.", row->mapDataId);
					return false;
				}

				const GameData::Monster::SMonsterData* const monster = monsterDataTable.Find(spawner->monsterDataId);
				const GameData::SpawnArea::SSpawnAreaData* const spawnArea = spawnAreaDataTable.Find(spawner->spawnAreaDataId);
				if (monster == nullptr || spawnArea == nullptr || spawnArea->mapDataId != row->mapDataId)
				{
					outError = std::format("SpawnerDataId {} could not be converted because its Monster or SpawnArea reference is invalid.",
						spawner->spawnerDataId);
					return false;
				}

				WorldCore::SMonsterSpawnerRuntimeDefinition runtimeSpawner{};
				runtimeSpawner.spawnerDataId = spawner->spawnerDataId;
				runtimeSpawner.mapDataId = spawner->mapDataId;
				runtimeSpawner.monsterSnapshot.monsterDataId = monster->monsterDataId;
				switch (monster->monsterType)
				{
					case GameData::Common::EMonsterType::Normal:
						runtimeSpawner.monsterSnapshot.monsterType = WorldCore::EMonsterType::Normal;
						break;
					case GameData::Common::EMonsterType::Boss:
						runtimeSpawner.monsterSnapshot.monsterType = WorldCore::EMonsterType::Boss;
						break;
					default:
						outError = std::format("MonsterDataId {} has an unknown MonsterType.", monster->monsterDataId);
						return false;
				}
				switch (monster->aggroType)
				{
					case GameData::Common::EMonsterAggroType::Aggressive:
						runtimeSpawner.monsterSnapshot.aggroType = WorldCore::EMonsterAggroType::Aggressive;
						break;
					case GameData::Common::EMonsterAggroType::Passive:
						runtimeSpawner.monsterSnapshot.aggroType = WorldCore::EMonsterAggroType::Passive;
						break;
					default:
						outError = std::format("MonsterDataId {} has an unknown MonsterAggroType.", monster->monsterDataId);
						return false;
				}
				runtimeSpawner.monsterSnapshot.maxHp = monster->maxHp;
				runtimeSpawner.monsterSnapshot.attack = monster->attack;
				runtimeSpawner.monsterSnapshot.defense = monster->defense;
				runtimeSpawner.monsterSnapshot.moveSpeed = monster->moveSpeed;
				runtimeSpawner.monsterSnapshot.collisionRadius = monster->collisionRadius;
				runtimeSpawner.monsterSnapshot.aggroRadius = monster->aggroRadius;
				runtimeSpawner.monsterSnapshot.leashRadius = monster->leashRadius;
				runtimeSpawner.monsterSnapshot.attackRange = monster->attackRange;
				runtimeSpawner.monsterSnapshot.attackCooldownMilliseconds = monster->attackCooldownMilliseconds;
				const std::uint64_t attackCooldownMilliseconds = monster->attackCooldownMilliseconds;
				if (attackCooldownMilliseconds >
					(std::numeric_limits<std::uint64_t>::max() - 999ull) / static_cast<std::uint64_t>(mapTickFps))
				{
					outError = std::format("MonsterDataId {} attack Cooldown overflowed the Tick conversion.", monster->monsterDataId);
					return false;
				}
				runtimeSpawner.monsterSnapshot.attackCooldownTicks =
					std::max<std::uint64_t>(1ull, (attackCooldownMilliseconds * static_cast<std::uint64_t>(mapTickFps) + 999ull) / 1000ull);
				runtimeSpawner.areaMinimum = {spawnArea->minX, spawnArea->minY};
				runtimeSpawner.areaMaximum = {spawnArea->maxX, spawnArea->maxY};
				runtimeSpawner.initialSpawnCount = spawner->initialSpawnCount;
				runtimeSpawner.maxAliveCount = spawner->maxAliveCount;

				const std::uint64_t respawnMilliseconds = spawner->respawnIntervalMilliseconds;
				if (respawnMilliseconds > (std::numeric_limits<std::uint64_t>::max() - 999ull) / static_cast<std::uint64_t>(mapTickFps))
				{
					outError = std::format("SpawnerDataId {} respawn delay overflowed the Tick conversion.", spawner->spawnerDataId);
					return false;
				}
				runtimeSpawner.respawnDelayTicks =
					std::max<std::uint64_t>(1ull, (respawnMilliseconds * static_cast<std::uint64_t>(mapTickFps) + 999ull) / 1000ull);
				bootMap.monsterSpawners.push_back(runtimeSpawner);
			}

			outDefinitions.push_back(bootMap);
		}

		if (outDefinitions.empty())
		{
			outError = "no boot map definitions were created.";
			return false;
		}

		outError.clear();
		return true;
	}

	class FWorldApplication final : public NetworkLib::IApplicationHandler
	{
	public:
		FWorldApplication(
			std::shared_ptr<Foundation::ILogger> logger,
			const ContentsRuntime::Core::SContentRuntimeConfig& contentRuntimeConfig,
			const SWorldRuntimeConfig& worldRuntimeConfig,
			const std::vector<WorldServer::Contents::SBootMapDefinition>& bootMaps,
			std::shared_ptr<Connector::ILoginTicketStore> loginTicketStore,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			std::shared_ptr<const WorldServer::Domain::FPlayerStatCalculator> playerStatCalculator)
			: m_logger(std::move(logger))
			, m_sessionRegistry(std::make_shared<WorldServer::Contents::FWorldSessionRegistry>())
			, m_loginTicketStore(std::move(loginTicketStore))
			, m_cacheRpcClient(std::move(cacheRpcClient))
			, m_playerStatCalculator(std::move(playerStatCalculator))
			, m_cacheEnabled(worldRuntimeConfig.cacheEnabled)
		{
			if (m_cacheRpcClient == nullptr)
			{
				throw std::invalid_argument("World cache RPC client is null.");
			}
			if (m_playerStatCalculator == nullptr)
			{
				throw std::invalid_argument("World player stat calculator is null.");
			}
			m_contentRuntime.SetConfig(contentRuntimeConfig);
			ContentsRuntime::Core::FContentInstanceIdAllocator allocator;
			const std::uint32_t executorInstanceCount = worldRuntimeConfig.sectorExecutorInstanceCount;
			std::vector<ContentsRuntime::Core::FContentInstanceId> executorInstanceIds;
			executorInstanceIds.reserve(executorInstanceCount);
			for (std::uint32_t executorIndex = 0; executorIndex < executorInstanceCount; ++executorIndex)
			{
				const auto instanceId = allocator.Allocate(WorldServer::Contents::kSectorExecutorContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(instanceId))
				{
					throw std::runtime_error("sector executor content instance id allocation failed.");
				}
				executorInstanceIds.push_back(instanceId);
			}
			m_taskGraphExecutionService = std::make_shared<WorldServer::Contents::FTaskGraphSectorExecutionService>(
				m_logger, executorInstanceIds, worldRuntimeConfig.sectorTaskPumpBatchSize);
			for (const ContentsRuntime::Core::FContentInstanceId instanceId : executorInstanceIds)
			{
				if (!m_contentRuntime.RegisterContent(
						std::make_unique<WorldServer::Contents::FSectorExecutorContent>(instanceId, kDefaultSectorExecutorMailboxCapacity)))
				{
					throw std::runtime_error("sector executor content registration failed.");
				}
			}

			std::vector<ContentsRuntime::Core::FContentInstanceId> shardInstanceIds;
			shardInstanceIds.reserve(worldRuntimeConfig.mapContentShardCount);
			for (std::uint32_t shardIndex = 0; shardIndex < worldRuntimeConfig.mapContentShardCount; ++shardIndex)
			{
				const auto instanceId = allocator.Allocate(WorldServer::Contents::kMapContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(instanceId))
				{
					throw std::runtime_error("map content instance id allocation failed.");
				}
				shardInstanceIds.push_back(instanceId);
			}

			std::vector<std::vector<WorldServer::Contents::SBootMapDefinition>> mapsByShard(worldRuntimeConfig.mapContentShardCount);
			std::vector<WorldServer::Contents::SMapRoute> routes;
			routes.reserve(bootMaps.size());
			for (const auto& bootMap : bootMaps)
			{
				const std::size_t shardIndex =
					WorldServer::Contents::GetMapContentShardIndex(bootMap.mapInstanceId, worldRuntimeConfig.mapContentShardCount);
				mapsByShard[shardIndex].push_back(bootMap);
				routes.push_back(
					{bootMap.mapDataId, bootMap.mapInstanceId, static_cast<std::uint32_t>(shardIndex), shardInstanceIds[shardIndex]});
			}

			WorldServer::Contents::SCachePresenceConfig cachePresenceConfig;
			cachePresenceConfig.enabled = worldRuntimeConfig.cacheEnabled;
			cachePresenceConfig.cacheServerInstanceId = worldRuntimeConfig.cacheServerInstanceId;
			cachePresenceConfig.rpcTimeout = worldRuntimeConfig.cacheRpcTimeout;
			cachePresenceConfig.retryInterval = worldRuntimeConfig.cacheReconnectInterval;

			for (std::uint32_t shardIndex = 0; shardIndex < worldRuntimeConfig.mapContentShardCount; ++shardIndex)
			{
				auto mapContent = std::make_unique<WorldServer::Contents::FMapContentShard>(m_logger,
					shardInstanceIds[shardIndex],
					shardIndex,
					worldRuntimeConfig.mapContentShardCount,
					worldRuntimeConfig.mapTickFps,
					kDefaultMapMailboxCapacity,
					m_sessionRegistry,
					m_taskGraphExecutionService,
					m_cacheRpcClient,
					cachePresenceConfig,
					m_playerStatCalculator);
				std::string initializeError;
				if (!mapContent->Initialize(mapsByShard[shardIndex], initializeError))
				{
					throw std::runtime_error(std::format("map shard {} initialization failed: {}", shardIndex, initializeError));
				}
				WorldServer::Contents::FMapContentShard* const mapContentPointer = mapContent.get();
				if (!m_contentRuntime.RegisterContent(std::move(mapContent)))
				{
					throw std::runtime_error(std::format("map shard {} registration failed.", shardIndex));
				}
				m_mapContentsByInstanceId.emplace(shardInstanceIds[shardIndex], mapContentPointer);
			}

			const auto routerInstanceId = allocator.Allocate(WorldServer::Contents::kMapRouterContentId);
			WorldServer::Contents::SWorldAuthConfig authConfig;
			authConfig.enabled = worldRuntimeConfig.authEnabled;
			authConfig.worldServerInstanceId = worldRuntimeConfig.rpcServerInstanceId;
			auto routerContent = std::make_unique<WorldServer::Contents::FMapRouterContent>(m_logger,
				routerInstanceId,
				routes,
				m_sessionRegistry,
				m_loginTicketStore,
				authConfig,
				m_cacheRpcClient,
				cachePresenceConfig,
				m_playerStatCalculator);
			m_mapRouterContent = routerContent.get();
			m_mapRouterContentInstanceId = routerInstanceId;
			if (!ContentsRuntime::Core::IsValidContentInstanceId(routerInstanceId) ||
				!m_contentRuntime.RegisterContent(std::move(routerContent)))
			{
				throw std::runtime_error("map router registration failed.");
			}

			m_cacheRpcClient->SetResponseCallback(
				[this](const std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcResponse& response)
				{
					bool enqueued = false;
					if (m_mapRouterContent != nullptr && response.originContentInstanceId == m_mapRouterContentInstanceId)
					{
						enqueued = m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
							[router = m_mapRouterContent, bridge = &m_contentRuntime, rpcSessionId, response]()
							{
								router->ProcessCacheRpcResponse(rpcSessionId, response, *bridge);
							});
					}
					else
					{
						const auto mapIt = m_mapContentsByInstanceId.find(response.originContentInstanceId);
						if (mapIt != m_mapContentsByInstanceId.end())
						{
							enqueued = m_contentRuntime.EnqueueCompletionToInstance(response.originContentInstanceId,
								[mapContent = mapIt->second, rpcSessionId, response]()
								{
									mapContent->ProcessCacheRpcResponse(rpcSessionId, response);
								});
						}
					}
					if (!enqueued)
					{
						Log(Foundation::ELogLevel::Warn,
							"World Cache response enqueue failed. requestId={} originContentInstanceId={}",
							response.requestId,
							response.originContentInstanceId);
					}
				});
			m_cacheRpcClient->SetRequestCallback(
				[this](const std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcRequest& request)
				{
					if (m_mapRouterContent == nullptr || !m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
															 [router = m_mapRouterContent, rpcSessionId, request]()
															 {
																 router->ProcessCacheRpcRequest(rpcSessionId, request);
															 }))
					{
						Log(Foundation::ELogLevel::Warn, "World Cache request enqueue failed. requestId={}", request.requestId);
					}
				});
			m_cacheRpcClient->SetNotificationCallback(
				[this](const std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcNotification& notification)
				{
					if (m_mapRouterContent == nullptr || !m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
															 [router = m_mapRouterContent, rpcSessionId, notification]()
															 {
																 router->ProcessCacheRpcNotification(rpcSessionId, notification);
															 }))
					{
						Log(Foundation::ELogLevel::Warn,
							"World Cache notification enqueue failed. serviceId={} methodId={}",
							notification.serviceId,
							notification.methodId);
					}
				});
			m_cacheRpcClient->SetDisconnectCallback(
				[this](const std::uint64_t rpcSessionId)
				{
					if (m_mapRouterContent != nullptr)
					{
						(void)m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
							[router = m_mapRouterContent, rpcSessionId]()
							{
								router->FailCacheRpcSession(rpcSessionId);
							});
					}
					for (const auto& [contentInstanceId, mapContent] : m_mapContentsByInstanceId)
					{
						(void)m_contentRuntime.EnqueueCompletionToInstance(contentInstanceId,
							[mapContent, rpcSessionId]()
							{
								mapContent->FailCacheRpcSession(rpcSessionId);
							});
					}
				});
			m_cacheRpcClient->SetReadyCallback(
				[this](const std::uint64_t rpcSessionId)
				{
					if (m_mapRouterContent != nullptr)
					{
						(void)m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
							[router = m_mapRouterContent, rpcSessionId]()
							{
								router->NotifyCacheReady(rpcSessionId);
							});
					}
				});

			for (const auto& route : routes)
			{
				Log(Foundation::ELogLevel::Info,
					"boot map assigned. mapDataId={} mapInstanceId={} shardIndex={} contentInstanceId={}",
					route.mapDataId,
					route.mapInstanceId,
					route.shardIndex,
					route.contentInstanceId);
			}
		}

		void OnServerStarted(
			NetworkLib::IServer& server) override
		{
			m_server = &server;
			if (m_taskGraphExecutionService != nullptr)
			{
				m_taskGraphExecutionService->BindBridge(m_contentRuntime);
			}
			m_contentRuntime.Start(server);
			if (m_cacheEnabled)
			{
				std::string cacheRpcError;
				if (!m_cacheRpcClient->Start(cacheRpcError))
				{
					Log(Foundation::ELogLevel::Error, "World Cache RPC client start failed: " + cacheRpcError);
				}
			}
			Log(Foundation::ELogLevel::Info,
				"WorldServer started. backend={} sectorExecutorInstances={}",
				static_cast<std::uint32_t>(server.GetBackendKind()),
				m_taskGraphExecutionService != nullptr ? m_taskGraphExecutionService->GetExecutorInstanceCount() : 0);
		}

		void OnClientConnected(
			const std::uint64_t sessionId) override
		{
			if (m_sessionRegistry == nullptr || !m_sessionRegistry->Add(sessionId) ||
				!m_contentRuntime.EnterSession(sessionId, WorldServer::Contents::kMapRouterContentId))
			{
				Log(Foundation::ELogLevel::Error, "map router enter failed. sessionId={}", sessionId);
				if (m_sessionRegistry != nullptr)
				{
					m_sessionRegistry->Remove(sessionId);
				}
				if (m_server != nullptr)
				{
					m_server->Disconnect(sessionId);
				}
			}
		}

		void OnPacketReceived(
			NetworkLib::IServer&,
			const std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			if (!m_contentRuntime.EnqueuePacket(sessionId, packetView.opcode, packetView.payload, packetView.payloadLength))
			{
				Log(Foundation::ELogLevel::Warn, "packet enqueue failed. sessionId={} opcode={}", sessionId, packetView.opcode);
			}
		}

		void OnClientDisconnected(
			const std::uint64_t sessionId) override
		{
			std::shared_ptr<WorldServer::Contents::FWorldSession> session;
			if (m_sessionRegistry != nullptr)
			{
				session = m_sessionRegistry->Find(sessionId);
				m_sessionRegistry->MarkDisconnected(sessionId);
			}
			m_contentRuntime.LeaveSession(sessionId);
			if (m_cacheEnabled && session != nullptr && m_mapRouterContent != nullptr)
			{
				(void)m_contentRuntime.EnqueueCompletionToInstance(m_mapRouterContentInstanceId,
					[router = m_mapRouterContent, session = std::move(session)]() mutable
					{
						router->NotifyClientDisconnected(std::move(session));
					});
			}
			if (m_sessionRegistry != nullptr)
			{
				m_sessionRegistry->Remove(sessionId);
			}
		}

		void OnServerStopped() override
		{
			m_cacheRpcClient->Stop();
			if (m_taskGraphExecutionService != nullptr)
			{
				m_taskGraphExecutionService->BeginShutdown();
			}
			m_contentRuntime.Stop();
			if (m_taskGraphExecutionService != nullptr)
			{
				const WorldServer::Contents::STaskGraphSectorExecutionStats stats = m_taskGraphExecutionService->GetStatsSnapshot();
				Log(Foundation::ELogLevel::Info,
					"TaskGraph stopped. started={} completed={} failed={} canceled={} tasks={} maxParallel={} workerMask=0x{:X}",
					stats.startedExecutionCount,
					stats.completedExecutionCount,
					stats.failedExecutionCount,
					stats.canceledExecutionCount,
					stats.executedTaskCount,
					stats.maxParallelTaskCount,
					stats.workerMask);
				m_taskGraphExecutionService->UnbindBridge(m_contentRuntime);
			}
			m_server = nullptr;
			Log(Foundation::ELogLevel::Info, "WorldServer stopped.");
		}

	private:
		void Log(
			const Foundation::ELogLevel level,
			const std::string& message) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "WorldServer", message);
			}
		}

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			const Foundation::ELogLevel level,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "WorldServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		std::shared_ptr<WorldServer::Contents::FWorldSessionRegistry> m_sessionRegistry;
		std::shared_ptr<WorldServer::Contents::FTaskGraphSectorExecutionService> m_taskGraphExecutionService;
		std::shared_ptr<Connector::ILoginTicketStore> m_loginTicketStore;
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		std::shared_ptr<const WorldServer::Domain::FPlayerStatCalculator> m_playerStatCalculator;
		WorldServer::Contents::FMapRouterContent* m_mapRouterContent = nullptr;
		ContentsRuntime::Core::FContentInstanceId m_mapRouterContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		std::unordered_map<ContentsRuntime::Core::FContentInstanceId, WorldServer::Contents::FMapContentShard*> m_mapContentsByInstanceId;
		bool m_cacheEnabled = false;
		ContentsRuntime::Routing::FContentRuntime m_contentRuntime;
		NetworkLib::IServer* m_server = nullptr;
	};
}

int WorldServer::Application::RunWorldServer(
	const int argc,
	char* argv[])
{
	SCommandLineOptions commandLineOptions{};
	if (!TryParseCommandLine(argc, argv, commandLineOptions))
	{
		std::cerr << "Usage: WorldServer [--config path] [--run-seconds N]\n";
		return 1;
	}

	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	const std::filesystem::path configPath = commandLineOptions.configPath.value_or(ResolveDefaultConfigPath(executableDirectory));
	Generated::Config::WorldServer::FWorldServerConfigDocument configDocument{};
	std::string configError;
	if (!Generated::Config::WorldServer::FWorldServerConfigLoader::LoadFromFile(configPath, configDocument, configError))
	{
		std::cerr << "WorldServer config load failed: " << configError << '\n';
		return 1;
	}

	NetworkLib::Core::SServerConfig serverConfig{};
	ContentsRuntime::Core::SContentRuntimeConfig contentRuntimeConfig{};
	SWorldRuntimeConfig worldRuntimeConfig{};
	std::uint32_t packetKey = 0;
	if (!ApplyConfig(configDocument, executableDirectory, serverConfig, contentRuntimeConfig, worldRuntimeConfig, packetKey, configError))
	{
		std::cerr << "WorldServer config apply failed: " << configError << '\n';
		return 1;
	}

	auto logger = std::make_shared<Foundation::FCompositeLogger>();
	logger->AddSink(std::make_shared<Foundation::FConsoleLogger>(serverConfig.logConfig));
	logger->AddSink(std::make_shared<Foundation::FFileLogger>(serverConfig.logConfig));
	serverConfig.logger = logger;

	NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
	cipherConfig.packetKey = static_cast<std::uint8_t>(packetKey);
	serverConfig.packetCipher = std::make_shared<NetworkLib::Crypto::FDefaultPacketCipher>(cipherConfig);
	serverConfig.packetFramer = std::make_shared<NetworkLib::Packet::Framing::FDefaultPacketFramer>();

	const std::filesystem::path mapDataPath = worldRuntimeConfig.gameDataDirectory / "Map.yaml";
	GameData::Map::FMapDataTable mapDataTable;
	std::string mapDataError;
	if (!mapDataTable.Load(mapDataPath, mapDataError))
	{
		logger->Log(
			Foundation::ELogLevel::Error, "WorldServer", "map data load failed. path={} error={}", mapDataPath.string(), mapDataError);
		logger->Flush();
		return 1;
	}

	GameData::Monster::FMonsterDataTable monsterDataTable;
	GameData::CombatFormulaPolicy::FCombatFormulaPolicyTable combatFormulaPolicyTable;
	GameData::SpawnArea::FSpawnAreaDataTable spawnAreaDataTable;
	GameData::MonsterSpawner::FMonsterSpawnerDataTable monsterSpawnerDataTable;
	const auto loadMonsterGameData = [&](auto& table, const std::string_view fileName)
	{
		const std::filesystem::path path = worldRuntimeConfig.gameDataDirectory / fileName;
		std::string loadError;
		if (table.Load(path, loadError))
		{
			return true;
		}
		logger->Log(Foundation::ELogLevel::Error, "WorldServer", "game data load failed. path={} error={}", path.string(), loadError);
		return false;
	};
	if (!loadMonsterGameData(combatFormulaPolicyTable, "CombatFormulaPolicy.yaml") ||
		!loadMonsterGameData(monsterDataTable, "Monster.yaml") || !loadMonsterGameData(spawnAreaDataTable, "SpawnArea.yaml") ||
		!loadMonsterGameData(monsterSpawnerDataTable, "MonsterSpawner.yaml"))
	{
		logger->Flush();
		return 1;
	}
	if (!spawnAreaDataTable.ValidateMaps(mapDataTable, mapDataError) ||
		!monsterSpawnerDataTable.ValidateReferences(mapDataTable, monsterDataTable, spawnAreaDataTable, mapDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "WorldServer", "monster spawn game data validation failed: {}", mapDataError);
		logger->Flush();
		return 1;
	}

	auto characterDataTable = std::make_shared<GameData::Character::FCharacterDataTable>();
	auto characterLevelDataTable = std::make_shared<GameData::CharacterLevel::FCharacterLevelDataTable>();
	auto itemDataTable = std::make_shared<GameData::Item::FItemDataTable>();
	auto statConversionTable = std::make_shared<GameData::StatConversion::FStatConversionTable>();
	const auto loadPlayerGameData = [&](auto& table, const std::string_view fileName)
	{
		const std::filesystem::path path = worldRuntimeConfig.gameDataDirectory / fileName;
		std::string loadError;
		if (table.Load(path, loadError))
		{
			return true;
		}
		logger->Log(Foundation::ELogLevel::Error, "WorldServer", "game data load failed. path={} error={}", path.string(), loadError);
		return false;
	};
	if (!loadPlayerGameData(*characterDataTable, "Character.yaml") ||
		!loadPlayerGameData(*characterLevelDataTable, "CharacterLevel.yaml") || !loadPlayerGameData(*itemDataTable, "Item.yaml") ||
		!loadPlayerGameData(*statConversionTable, "StatConversion.yaml"))
	{
		logger->Flush();
		return 1;
	}
	std::string playerDataValidationError;
	if (characterDataTable->Size() == 0 || itemDataTable->Size() == 0 || statConversionTable->Size() == 0 ||
		!characterLevelDataTable->ValidateCharacters(*characterDataTable, playerDataValidationError))
	{
		logger->Log(Foundation::ELogLevel::Error,
			"WorldServer",
			"player game data validation failed: {}",
			playerDataValidationError.empty() ? "a required table is empty." : playerDataValidationError);
		logger->Flush();
		return 1;
	}
	auto playerStatCalculator = std::make_shared<WorldServer::Domain::FPlayerStatCalculator>(
		std::move(characterDataTable), std::move(characterLevelDataTable), std::move(itemDataTable), std::move(statConversionTable));

	std::vector<WorldServer::Contents::SBootMapDefinition> bootMaps;
	if (!BuildBootMapDefinitions(mapDataTable,
			combatFormulaPolicyTable.Get(),
			monsterDataTable,
			spawnAreaDataTable,
			monsterSpawnerDataTable,
			worldRuntimeConfig.movementCorrectionTolerance,
			worldRuntimeConfig.mapTickFps,
			bootMaps,
			mapDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "WorldServer", "boot map conversion failed: {}", mapDataError);
		logger->Flush();
		return 1;
	}

	try
	{
		RpcLib::Client::FOutboundRpcClientConfig cacheRpcConfig;
		cacheRpcConfig.host = worldRuntimeConfig.cacheHost;
		cacheRpcConfig.port = worldRuntimeConfig.cachePort;
		cacheRpcConfig.localServerType = RpcLib::Protocol::ERpcServerType::Game;
		cacheRpcConfig.localServerInstanceId = worldRuntimeConfig.rpcServerInstanceId;
		cacheRpcConfig.expectedRemoteServerType = RpcLib::Protocol::ERpcServerType::Cache;
		cacheRpcConfig.expectedRemoteServerInstanceId = worldRuntimeConfig.cacheServerInstanceId;
		cacheRpcConfig.packetKey = worldRuntimeConfig.cacheRpcPacketKey;
		cacheRpcConfig.randomKey = kRpcRandomKey;
		cacheRpcConfig.handshakeTimeout = worldRuntimeConfig.cacheRpcTimeout;
		cacheRpcConfig.reconnectInterval = worldRuntimeConfig.cacheReconnectInterval;
		auto cacheRpcClient = std::make_shared<RpcLib::Client::FOutboundRpcClient>(std::move(cacheRpcConfig));
		std::shared_ptr<Connector::ILoginTicketStore> loginTicketStore;
		if (worldRuntimeConfig.authEnabled)
		{
			loginTicketStore = std::make_shared<Connector::FRedisLoginTicketStore>(worldRuntimeConfig.loginTicketStoreConfig);
		}

		FWorldApplication application(logger,
			contentRuntimeConfig,
			worldRuntimeConfig,
			bootMaps,
			std::move(loginTicketStore),
			std::move(cacheRpcClient),
			std::move(playerStatCalculator));
		std::unique_ptr<NetworkLib::IServer> server = NetworkLib::Core::FServerFactory::Create(serverConfig.backendKind);
		if (server == nullptr || !server->Start(serverConfig, application))
		{
			logger->Log(Foundation::ELogLevel::Error, "WorldServer", "server start failed.");
			logger->Flush();
			return 1;
		}

		logger->Log(Foundation::ELogLevel::Info,
			"WorldServer",
			"boot completed. bind={}:{} maps={} mapShards={} mapTickFps={} mapDataPath={}",
			serverConfig.bindIp,
			serverConfig.port,
			bootMaps.size(),
			worldRuntimeConfig.mapContentShardCount,
			worldRuntimeConfig.mapTickFps,
			mapDataPath.string());

		if (commandLineOptions.runSeconds > 0)
		{
			std::this_thread::sleep_for(std::chrono::seconds(commandLineOptions.runSeconds));
		}
		else if (worldRuntimeConfig.headless)
		{
			logger->Log(Foundation::ELogLevel::Info, "WorldServer", "headless mode enabled.");
			while (true)
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
		else
		{
			std::cout << "Press ENTER to stop.\n";
			std::cin.get();
		}

		server->Stop();
		logger->Flush();
		return 0;
	}
	catch (const std::exception& exception)
	{
		logger->Log(Foundation::ELogLevel::Error, "WorldServer", "startup failed: {}", exception.what());
		logger->Flush();
		return 1;
	}
}
