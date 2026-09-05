#include "CacheServerPch.h"

#include "CacheServer/Application/FCacheServerBootstrap.h"

#include "CacheServer/Contents/ContentTypes.h"
#include "CacheServer/Contents/FPlayerCacheContent.h"
#include "CacheServer/Contents/FRpcRouterContent.h"
#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "Foundation/Logging/FCompositeLogger.h"
#include "Foundation/Logging/FConsoleLogger.h"
#include "Foundation/Logging/FFileLogger.h"
#include "Generated/Config/CacheServer/CacheServerConfig.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FServerFactory.h"
#include "Servers/IApplicationHandler.h"

namespace
{
	std::filesystem::path GetExecutableDirectory()
	{
		std::array<char, MAX_PATH> modulePath{};
		const DWORD length = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (length == 0 || length >= modulePath.size())
		{
			throw std::runtime_error("executable path lookup failed.");
		}
		return std::filesystem::path(std::string(modulePath.data(), length)).parent_path();
	}

	struct SCommandLineOptions final
	{
		std::optional<std::filesystem::path> configPath;
	};

	struct SCacheRuntimeConfig final
	{
		std::uint32_t contentsWorkerThreadCount = 1;
		std::uint32_t playerCacheShardCount = 1;
		std::uint64_t mailboxCapacity = 1;
		RpcLib::Protocol::FRpcServerInstanceId serverInstanceId = 1;
		std::filesystem::path gameDataDirectory;
		CacheServer::Contents::SPlayerCachePolicy cachePolicy;
		bool faultInjectionCreditBeforeDatabaseTransaction = false;
		bool faultInjectionCreditAfterCommitDisconnect = false;
		std::uint32_t faultInjectionCreditBeforeDatabaseDelayMilliseconds = 0;
		std::uint32_t faultInjectionCreditAfterCommitDelayMilliseconds = 0;
		std::uint32_t runSeconds = 0;
		bool headless = false;
	};

	bool TryParseCommandLine(
		const int argc,
		char* argv[],
		SCommandLineOptions& outOptions)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument = argv[index];
			if (argument == "--config" && index + 1 < argc)
			{
				outOptions.configPath = std::filesystem::path(argv[++index]);
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	std::filesystem::path ResolveDefaultConfigPath(
		const std::filesystem::path& executableDirectory)
	{
		const std::filesystem::path configDirectory = executableDirectory / "Config" / "Server";
		const std::filesystem::path localConfigPath = configDirectory / "CacheServer.local.yaml";
		std::error_code errorCode;
		if (std::filesystem::is_regular_file(localConfigPath, errorCode))
		{
			return localConfigPath;
		}

		return configDirectory / "CacheServer.yaml";
	}

	std::filesystem::path ResolveGameDataDirectory(
		const std::filesystem::path& executableDirectory,
		const std::string& configuredDirectory)
	{
		const std::filesystem::path directory(configuredDirectory);
		return directory.is_absolute() ? directory : executableDirectory / "Config" / directory;
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
		const Generated::Config::CacheServer::EBackend backend) noexcept
	{
		return backend == Generated::Config::CacheServer::EBackend::Rio ? NetworkLib::Core::EBackendKind::Rio
																		: NetworkLib::Core::EBackendKind::Iocp;
	}

	NetworkLib::Core::ERioSendDispatchMode ToRioSendDispatchMode(
		const Generated::Config::CacheServer::ERioSendDispatchMode mode) noexcept
	{
		return mode == Generated::Config::CacheServer::ERioSendDispatchMode::OwnerThread
				   ? NetworkLib::Core::ERioSendDispatchMode::OwnerThread
				   : NetworkLib::Core::ERioSendDispatchMode::Direct;
	}

	Foundation::ELogLevel ToLogLevel(
		const Generated::Config::CacheServer::ELogMinimumLevel level) noexcept
	{
		switch (level)
		{
			case Generated::Config::CacheServer::ELogMinimumLevel::Debug:
				return Foundation::ELogLevel::Debug;
			case Generated::Config::CacheServer::ELogMinimumLevel::Warn:
				return Foundation::ELogLevel::Warn;
			case Generated::Config::CacheServer::ELogMinimumLevel::Error:
				return Foundation::ELogLevel::Error;
			case Generated::Config::CacheServer::ELogMinimumLevel::Info:
			default:
				return Foundation::ELogLevel::Info;
		}
	}

	bool ApplyConfig(
		const Generated::Config::CacheServer::FCacheServerConfigDocument& document,
		const std::filesystem::path& executableDirectory,
		NetworkLib::Core::SServerConfig& outServerConfig,
		SCacheRuntimeConfig& outRuntimeConfig,
		CacheServer::Database::SCacheDatabaseConfig& outDatabaseConfig,
		std::uint32_t& outPacketKey,
		std::string& outError)
	{
		const auto& config = document.CacheServer;
		if (config.BindIp.empty() || config.Port == 0 || config.WorkerThreadCount <= 0 || config.MaxSessionCount <= 0 ||
			config.RecvBufferSize <= 0)
		{
			outError = "CacheServer network endpoint and capacity values are invalid.";
			return false;
		}
		if (config.PacketKey > 0xFFu)
		{
			outError = "CacheServer.PacketKey must be in range 0..255.";
			return false;
		}
		if (config.ContentsWorkerThreadCount <= 0 || config.PlayerCacheShardCount == 0 || config.PlayerCacheShardCount > 64 ||
			config.MailboxCapacity == 0 || config.RpcServerInstanceId == 0)
		{
			outError = "CacheServer content worker, shard, mailbox, or instance configuration is invalid.";
			return false;
		}
		if (config.GameDataDirectory.empty())
		{
			outError = "CacheServer.GameDataDirectory must not be empty.";
			return false;
		}
		if (config.ReplicaReconnectCooldownMilliseconds == 0 || config.ReplicaReconnectCooldownMilliseconds > 3600000)
		{
			outError = "CacheServer.ReplicaReconnectCooldownMilliseconds must be in range 1..3600000.";
			return false;
		}

		const auto& policy = document.CachePolicy;
		if (policy.GameOwnerLeaseMilliseconds == 0 || policy.GameOwnerLeaseMilliseconds > 86400000 ||
			policy.IdleEvictionMilliseconds == 0 || policy.IdleEvictionMilliseconds > 86400000 ||
			policy.MaintenanceIntervalMilliseconds == 0 || policy.MaintenanceIntervalMilliseconds > 60000 ||
			policy.RevokeTimeoutMilliseconds == 0 || policy.RevokeTimeoutMilliseconds > 60000)
		{
			outError = "CachePolicy duration is outside the supported range.";
			return false;
		}

		const auto& faultInjection = document.FaultInjection;
		if (faultInjection.CreditBeforeDatabaseDelayMilliseconds > 60000 || faultInjection.CreditAfterCommitDelayMilliseconds > 60000)
		{
			outError = "FaultInjection delay must not exceed 60000 milliseconds.";
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

		outRuntimeConfig.contentsWorkerThreadCount = static_cast<std::uint32_t>(config.ContentsWorkerThreadCount);
		outRuntimeConfig.playerCacheShardCount = config.PlayerCacheShardCount;
		outRuntimeConfig.mailboxCapacity = config.MailboxCapacity;
		outRuntimeConfig.serverInstanceId = config.RpcServerInstanceId;
		outRuntimeConfig.gameDataDirectory = ResolveGameDataDirectory(executableDirectory, config.GameDataDirectory);
		outRuntimeConfig.cachePolicy.gameOwnerLeaseDuration = std::chrono::milliseconds(policy.GameOwnerLeaseMilliseconds);
		outRuntimeConfig.cachePolicy.idleEvictionDuration = std::chrono::milliseconds(policy.IdleEvictionMilliseconds);
		outRuntimeConfig.cachePolicy.maintenanceInterval = std::chrono::milliseconds(policy.MaintenanceIntervalMilliseconds);
		outRuntimeConfig.cachePolicy.revokeTimeout = std::chrono::milliseconds(policy.RevokeTimeoutMilliseconds);
		outRuntimeConfig.faultInjectionCreditBeforeDatabaseTransaction = faultInjection.CreditBeforeDatabaseTransaction;
		outRuntimeConfig.faultInjectionCreditAfterCommitDisconnect = faultInjection.CreditAfterCommitDisconnect;
		outRuntimeConfig.faultInjectionCreditBeforeDatabaseDelayMilliseconds = faultInjection.CreditBeforeDatabaseDelayMilliseconds;
		outRuntimeConfig.faultInjectionCreditAfterCommitDelayMilliseconds = faultInjection.CreditAfterCommitDelayMilliseconds;
		outRuntimeConfig.runSeconds = document.Debug.RunSeconds;
		outRuntimeConfig.headless = document.Debug.Headless;

		outDatabaseConfig.enabled = config.DatabaseEnabled;
		outDatabaseConfig.replicaReconnectCooldownMilliseconds = config.ReplicaReconnectCooldownMilliseconds;
		if (config.DatabaseEnabled)
		{
			const std::string& password = document.GameDatabase.Password;
			if (password.empty())
			{
				outError = "GameDatabase.Password must not be empty when CacheServer.DatabaseEnabled is true.";
				return false;
			}

			const auto& primary = document.GamePrimary;
			if (primary.Host.empty() || primary.Port == 0 || primary.User.empty() || primary.Database.empty() ||
				primary.ConnectTimeoutSeconds == 0)
			{
				outError = "GamePrimary configuration is invalid.";
				return false;
			}
			outDatabaseConfig.gamePrimary = {
				primary.Host, primary.Port, primary.User, password, primary.Database, primary.ConnectTimeoutSeconds};

			const auto appendReplica = [&](const auto& replica, const std::string_view name)
			{
				if (!replica.Enabled)
				{
					return true;
				}
				if (replica.Host.empty() || replica.Port == 0 || replica.User.empty() || replica.Database.empty() ||
					replica.ConnectTimeoutSeconds == 0)
				{
					outError = std::string(name) + " configuration is invalid.";
					return false;
				}
				outDatabaseConfig.gameReplicas.push_back(
					{replica.Host, replica.Port, replica.User, password, replica.Database, replica.ConnectTimeoutSeconds});
				return true;
			};
			if (!appendReplica(document.GameReplica1, "GameReplica1") || !appendReplica(document.GameReplica2, "GameReplica2"))
			{
				return false;
			}
		}

		outPacketKey = config.PacketKey;
		outError.clear();
		return true;
	}

	class FCacheApplication final : public NetworkLib::IApplicationHandler
	{
	public:
		FCacheApplication(
			std::shared_ptr<Foundation::ILogger> logger,
			const RpcLib::Protocol::FRpcServerInstanceId serverInstanceId,
			const std::uint32_t contentsWorkerThreadCount,
			const std::uint32_t shardCount,
			const std::uint64_t mailboxCapacity,
			CacheServer::Database::SCacheDatabaseConfig databaseConfig,
			std::shared_ptr<const GameData::Character::FCharacterDataTable> characterDataTable,
			std::shared_ptr<const GameData::CharacterLevel::FCharacterLevelDataTable> characterLevelDataTable,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
			std::shared_ptr<const GameData::Currency::FCurrencyDataTable> currencyDataTable,
			std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
			std::shared_ptr<const GameData::MailTemplate::FMailTemplateTable> mailTemplateTable,
			const CacheServer::Contents::SPlayerCachePolicy cachePolicy,
			const bool faultInjectionCreditBeforeDatabaseTransaction,
			const bool faultInjectionCreditAfterCommitDisconnect,
			const std::uint32_t faultInjectionCreditBeforeDatabaseDelayMilliseconds,
			const std::uint32_t faultInjectionCreditAfterCommitDelayMilliseconds)
			: m_logger(std::move(logger))
			, m_databaseConfig(std::move(databaseConfig))
		{
			if (characterDataTable == nullptr || characterLevelDataTable == nullptr || itemDataTable == nullptr ||
				inventoryPolicyTable == nullptr || currencyDataTable == nullptr || mailPolicyTable == nullptr ||
				mailTemplateTable == nullptr)
			{
				throw std::invalid_argument("CacheServer GameData tables must not be null.");
			}

			ContentsRuntime::Core::SContentRuntimeConfig runtimeConfig{};
			runtimeConfig.workerThreadCount = contentsWorkerThreadCount;
			m_contentRuntime.SetConfig(runtimeConfig);

			ContentsRuntime::Core::FContentInstanceIdAllocator allocator;
			std::vector<ContentsRuntime::Core::FContentInstanceId> playerCacheInstanceIds;
			playerCacheInstanceIds.reserve(shardCount);
			for (std::uint32_t shardIndex = 0; shardIndex < shardCount; ++shardIndex)
			{
				const auto contentInstanceId = allocator.Allocate(CacheServer::Contents::kPlayerCacheContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(contentInstanceId))
				{
					throw std::runtime_error("player cache instance id allocation failed.");
				}

				playerCacheInstanceIds.push_back(contentInstanceId);
				if (!m_contentRuntime.RegisterContent(std::make_unique<CacheServer::Contents::FPlayerCacheContent>(m_logger,
						contentInstanceId,
						shardIndex,
						shardCount,
						mailboxCapacity,
						m_sessionRegistry,
						m_requestIdGenerator,
						m_ownerGenerationSequence,
						m_transport,
						m_databaseConfig,
						characterDataTable,
						characterLevelDataTable,
						itemDataTable,
						inventoryPolicyTable,
						currencyDataTable,
						mailPolicyTable,
						mailTemplateTable,
						cachePolicy,
						faultInjectionCreditBeforeDatabaseTransaction,
						faultInjectionCreditAfterCommitDisconnect,
						faultInjectionCreditBeforeDatabaseDelayMilliseconds,
						faultInjectionCreditAfterCommitDelayMilliseconds)))
				{
					throw std::runtime_error("player cache content registration failed.");
				}
			}

			const auto routerInstanceId = allocator.Allocate(CacheServer::Contents::kRpcRouterContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(routerInstanceId) ||
				!m_contentRuntime.RegisterContent(std::make_unique<CacheServer::Contents::FRpcRouterContent>(m_logger,
					routerInstanceId,
					serverInstanceId,
					mailboxCapacity,
					m_sessionRegistry,
					m_transport,
					std::move(playerCacheInstanceIds))))
			{
				throw std::runtime_error("RPC router content registration failed.");
			}
		}

		void OnServerStarted(
			NetworkLib::IServer& server) override
		{
			m_server = &server;
			m_transport.Bind(server);
			m_contentRuntime.Start(server);
			Log(Foundation::ELogLevel::Info, "CacheServer started.");
		}

		void OnClientConnected(
			const std::uint64_t sessionId) override
		{
			const auto connectedAt = std::chrono::steady_clock::now();
			bool initialized = m_sessionRegistry.Add(sessionId, connectedAt);
			if (initialized)
			{
				const std::shared_ptr<RpcLib::Session::FRpcSession> session = m_sessionRegistry.Find(sessionId);
				initialized = session != nullptr && session->BeginHandshake() &&
							  m_contentRuntime.EnterSession(sessionId, CacheServer::Contents::kRpcRouterContentId);
			}

			if (initialized)
			{
				return;
			}

			m_sessionRegistry.Remove(sessionId);
			if (m_server != nullptr)
			{
				m_server->Disconnect(sessionId);
			}
			Log(Foundation::ELogLevel::Error, "RPC session initialization failed. sessionId={}", sessionId);
		}

		void OnPacketReceived(
			NetworkLib::IServer&,
			const std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			if (!RpcLib::Protocol::IsRpcWireOpcode(packetView.opcode))
			{
				Log(Foundation::ELogLevel::Warn, "non-RPC packet rejected. sessionId={} opcode={}", sessionId, packetView.opcode);
				if (m_server != nullptr)
				{
					m_server->Disconnect(sessionId);
				}
				return;
			}

			if (!m_contentRuntime.EnqueuePacket(sessionId, packetView.opcode, packetView.payload, packetView.payloadLength))
			{
				Log(Foundation::ELogLevel::Warn, "RPC packet enqueue failed. sessionId={} opcode={}", sessionId, packetView.opcode);
			}
		}

		void OnClientDisconnected(
			const std::uint64_t sessionId) override
		{
			m_contentRuntime.LeaveSession(sessionId);
			m_sessionRegistry.Remove(sessionId);
		}

		void OnServerStopped() override
		{
			m_contentRuntime.Stop();
			m_transport.Unbind();
			m_server = nullptr;
			Log(Foundation::ELogLevel::Info, "CacheServer stopped.");
		}

	private:
		void Log(
			const Foundation::ELogLevel level,
			const std::string& message) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "CacheServer", message);
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
				m_logger->Log(level, "CacheServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		RpcLib::Session::FRpcSessionRegistry m_sessionRegistry;
		RpcLib::Call::FRpcRequestIdGenerator m_requestIdGenerator;
		std::atomic<std::uint64_t> m_ownerGenerationSequence = 1;
		RpcLib::Transport::FServerRpcTransport m_transport;
		ContentsRuntime::Routing::FContentRuntime m_contentRuntime;
		CacheServer::Database::SCacheDatabaseConfig m_databaseConfig;
		NetworkLib::IServer* m_server = nullptr;
	};
}

int CacheServer::Application::RunCacheServer(
	const int argc,
	char* argv[])
{
	SCommandLineOptions commandLineOptions;
	if (!TryParseCommandLine(argc, argv, commandLineOptions))
	{
		std::cerr << "Usage: CacheServer [--config path]\n";
		return 1;
	}

	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	const std::filesystem::path configPath = commandLineOptions.configPath.value_or(ResolveDefaultConfigPath(executableDirectory));
	Generated::Config::CacheServer::FCacheServerConfigDocument configDocument{};
	std::string configError;
	if (!Generated::Config::CacheServer::FCacheServerConfigLoader::LoadFromFile(configPath, configDocument, configError))
	{
		std::cerr << "CacheServer config load failed: " << configError << '\n';
		return 1;
	}

	NetworkLib::Core::SServerConfig serverConfig{};
	SCacheRuntimeConfig runtimeConfig{};
	CacheServer::Database::SCacheDatabaseConfig databaseConfig{};
	std::uint32_t packetKey = 0;
	if (!ApplyConfig(configDocument, executableDirectory, serverConfig, runtimeConfig, databaseConfig, packetKey, configError))
	{
		std::cerr << "CacheServer config apply failed: " << configError << '\n';
		return 1;
	}

	NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
	cipherConfig.packetKey = static_cast<std::uint8_t>(packetKey);
	serverConfig.packetCipher = std::make_shared<NetworkLib::Crypto::FDefaultPacketCipher>(cipherConfig);
	serverConfig.packetFramer = std::make_shared<NetworkLib::Packet::Framing::FDefaultPacketFramer>();

	auto logger = std::make_shared<Foundation::FCompositeLogger>();
	logger->AddSink(std::make_shared<Foundation::FConsoleLogger>(serverConfig.logConfig));
	logger->AddSink(std::make_shared<Foundation::FFileLogger>(serverConfig.logConfig));
	serverConfig.logger = logger;

	const std::filesystem::path& gameDataDirectory = runtimeConfig.gameDataDirectory;
	std::string gameDataError;

	auto characterDataTable = std::make_shared<GameData::Character::FCharacterDataTable>();
	const std::filesystem::path characterDataPath = gameDataDirectory / "Character.yaml";
	if (!characterDataTable->Load(characterDataPath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "character data load failed: " + gameDataError);
		return 1;
	}

	auto characterLevelDataTable = std::make_shared<GameData::CharacterLevel::FCharacterLevelDataTable>();
	const std::filesystem::path characterLevelDataPath = gameDataDirectory / "CharacterLevel.yaml";
	if (!characterLevelDataTable->Load(characterLevelDataPath, gameDataError) ||
		!characterLevelDataTable->ValidateCharacters(*characterDataTable, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "character level data load failed: " + gameDataError);
		return 1;
	}

	auto itemDataTable = std::make_shared<GameData::Item::FItemDataTable>();
	const std::filesystem::path itemDataPath = gameDataDirectory / "Item.yaml";
	if (!itemDataTable->Load(itemDataPath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "item data load failed: " + gameDataError);
		return 1;
	}

	auto inventoryPolicyTable = std::make_shared<GameData::InventoryPolicy::FInventoryPolicyTable>();
	const std::filesystem::path inventoryPolicyPath = gameDataDirectory / "InventoryPolicy.yaml";
	if (!inventoryPolicyTable->Load(inventoryPolicyPath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "inventory policy load failed: " + gameDataError);
		return 1;
	}

	auto currencyDataTable = std::make_shared<GameData::Currency::FCurrencyDataTable>();
	const std::filesystem::path currencyDataPath = gameDataDirectory / "Currency.yaml";
	if (!currencyDataTable->Load(currencyDataPath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "currency data load failed: " + gameDataError);
		return 1;
	}

	auto mailPolicyTable = std::make_shared<GameData::MailPolicy::FMailPolicyTable>();
	const std::filesystem::path mailPolicyPath = gameDataDirectory / "MailPolicy.yaml";
	if (!mailPolicyTable->Load(mailPolicyPath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "mail policy load failed: " + gameDataError);
		return 1;
	}

	auto mailTemplateTable = std::make_shared<GameData::MailTemplate::FMailTemplateTable>();
	const std::filesystem::path mailTemplatePath = gameDataDirectory / "MailTemplate.yaml";
	if (!mailTemplateTable->Load(mailTemplatePath, gameDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "mail template data load failed: " + gameDataError);
		return 1;
	}

	logger->Log(Foundation::ELogLevel::Info,
		"CacheServer",
		"GameData loaded. characters={} characterLevels={} items={} currencies={} inventoryPageSize={} "
		"maxInventorySlots={} mailPageSize={} mailTemplates={} path={}",
		characterDataTable->Size(),
		characterLevelDataTable->Size(),
		itemDataTable->Size(),
		currencyDataTable->Size(),
		inventoryPolicyTable->Get().inventoryListPageSize,
		inventoryPolicyTable->Get().maxInventorySlots,
		mailPolicyTable->Get().mailListPageSize,
		mailTemplateTable->Size(),
		gameDataDirectory.string());

	try
	{
		FCacheApplication application(logger,
			runtimeConfig.serverInstanceId,
			runtimeConfig.contentsWorkerThreadCount,
			runtimeConfig.playerCacheShardCount,
			runtimeConfig.mailboxCapacity,
			std::move(databaseConfig),
			std::move(characterDataTable),
			std::move(characterLevelDataTable),
			std::move(itemDataTable),
			std::move(inventoryPolicyTable),
			std::move(currencyDataTable),
			std::move(mailPolicyTable),
			std::move(mailTemplateTable),
			runtimeConfig.cachePolicy,
			runtimeConfig.faultInjectionCreditBeforeDatabaseTransaction,
			runtimeConfig.faultInjectionCreditAfterCommitDisconnect,
			runtimeConfig.faultInjectionCreditBeforeDatabaseDelayMilliseconds,
			runtimeConfig.faultInjectionCreditAfterCommitDelayMilliseconds);
		std::unique_ptr<NetworkLib::IServer> server = NetworkLib::Core::FServerFactory::Create(serverConfig.backendKind);
		if (server == nullptr || !server->Start(serverConfig, application))
		{
			logger->Log(Foundation::ELogLevel::Error, "CacheServer", "server start failed.");
			return 1;
		}

		logger->Log(Foundation::ELogLevel::Info,
			"CacheServer",
			"boot completed. bind={}:{} shards={} contentsWorkers={} databaseEnabled={} config={}",
			serverConfig.bindIp,
			serverConfig.port,
			runtimeConfig.playerCacheShardCount,
			runtimeConfig.contentsWorkerThreadCount,
			configDocument.CacheServer.DatabaseEnabled,
			configPath.string());
		if (runtimeConfig.runSeconds > 0)
		{
			std::this_thread::sleep_for(std::chrono::seconds(runtimeConfig.runSeconds));
		}
		else if (runtimeConfig.headless)
		{
			logger->Log(Foundation::ELogLevel::Info, "CacheServer", "headless mode enabled.");
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
	}
	catch (const std::exception& exception)
	{
		logger->Log(Foundation::ELogLevel::Error, "CacheServer", "startup failed: " + std::string(exception.what()));
		logger->Flush();
		return 1;
	}

	return 0;
}
