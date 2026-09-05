#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Application/FAuctionHouseServerBootstrap.h"

#include "AuctionHouseServer/Contents/Command/FAuctionCommandContent.h"
#include "AuctionHouseServer/Contents/Auth/FAuctionAuthContent.h"
#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/FAuctionContentRouter.h"
#include "AuctionHouseServer/Contents/Expiration/FAuctionExpirationContent.h"
#include "AuctionHouseServer/Contents/Session/FAuctionSessionRegistry.h"
#include "AuctionHouseServer/Diagnostics/FAuctionTimingSetup.h"
#include "Connector/Config/RedisChatTicketStoreTypes.h"
#include "Connector/Redis/FRedisChatTicketStore.h"
#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "Foundation/Logging/FCompositeLogger.h"
#include "Foundation/Logging/FConsoleLogger.h"
#include "Foundation/Logging/FFileLogger.h"
#include "Generated/Config/AuctionHouseServer/AuctionHouseServerConfig.h"
#include "Generated/Packets/Cpp/Auction/AuctionPackets.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "GameData/InventoryPolicy/FInventoryPolicyTable.h"
#include "GameData/Item/FItemDataTable.h"
#include "GameData/MailPolicy/FMailPolicyTable.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FServerFactory.h"
#include "Servers/IApplicationHandler.h"

#include <limits>
#include <stdexcept>

#include <format>
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

	bool TryResolveConfigPath(
		const int argc,
		char* argv[],
		const std::filesystem::path& executableDirectory,
		std::filesystem::path& outConfigPath)
	{
		if (argc == 1)
		{
			const std::filesystem::path localConfigPath = executableDirectory / "Config" / "Server" / "AuctionHouseServer.local.yaml";
			outConfigPath = std::filesystem::exists(localConfigPath)
								? localConfigPath
								: executableDirectory / "Config" / "Server" / "AuctionHouseServer.yaml";
			return true;
		}
		if (argc == 3 && std::string_view(argv[1]) == "--config" && argv[2] != nullptr && argv[2][0] != '\0')
		{
			outConfigPath = std::filesystem::path(argv[2]);
			return true;
		}
		return false;
	}

	std::filesystem::path ResolveRuntimePath(
		const std::filesystem::path& executableDirectory,
		const std::string& configuredPath)
	{
		const std::filesystem::path path(configuredPath);
		return path.is_absolute() ? path : executableDirectory / path;
	}

	NetworkLib::Core::EBackendKind ToBackendKind(
		const Generated::Config::AuctionHouseServer::EBackend backend) noexcept
	{
		switch (backend)
		{
			case Generated::Config::AuctionHouseServer::EBackend::Rio:
				return NetworkLib::Core::EBackendKind::Rio;
			case Generated::Config::AuctionHouseServer::EBackend::Iocp:
			default:
				return NetworkLib::Core::EBackendKind::Iocp;
		}
	}

	NetworkLib::Core::ERioSendDispatchMode ToRioSendDispatchMode(
		const Generated::Config::AuctionHouseServer::ERioSendDispatchMode mode) noexcept
	{
		return mode == Generated::Config::AuctionHouseServer::ERioSendDispatchMode::OwnerThread
				   ? NetworkLib::Core::ERioSendDispatchMode::OwnerThread
				   : NetworkLib::Core::ERioSendDispatchMode::Direct;
	}

	Foundation::ELogLevel ToLogLevel(
		const Generated::Config::AuctionHouseServer::ELoggingMinimumLevel level) noexcept
	{
		switch (level)
		{
			case Generated::Config::AuctionHouseServer::ELoggingMinimumLevel::Debug:
				return Foundation::ELogLevel::Debug;
			case Generated::Config::AuctionHouseServer::ELoggingMinimumLevel::Warn:
				return Foundation::ELogLevel::Warn;
			case Generated::Config::AuctionHouseServer::ELoggingMinimumLevel::Error:
				return Foundation::ELogLevel::Error;
			case Generated::Config::AuctionHouseServer::ELoggingMinimumLevel::Info:
			default:
				return Foundation::ELogLevel::Info;
		}
	}

	struct SAuctionAuthRuntimeConfig final
	{
		bool enabled = false;
		Connector::SRedisLoginTicketStoreConfig ticketStore;
	};

	class FAuctionApplication final : public NetworkLib::IApplicationHandler
	{
	public:
		FAuctionApplication(
			std::shared_ptr<Foundation::ILogger> logger,
			const std::uint32_t commandShardCount,
			const std::int32_t contentsWorkerThreadCount,
			const std::uint64_t commandMailboxCapacity,
			const std::uint32_t testDelayShardIndex,
			const std::uint32_t testDelayMilliseconds,
			const bool faultInjectionAfterAuctionCommit,
			const bool faultInjectionBidRefundBeforeComplete,
			const std::uint32_t expirationPollMilliseconds,
			SAuctionAuthRuntimeConfig authConfig,
			const int timingMetricsFlushIntervalSeconds,
			std::string timingCsvPath,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> inventoryPolicyTable,
			std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> mailPolicyTable,
			AuctionHouseServer::Database::SAuctionDatabaseConfig databaseConfig,
			std::shared_ptr<RpcLib::Client::FOutboundRpcClient> cacheRpcClient,
			const RpcLib::Protocol::FRpcServerInstanceId cacheServerInstanceId,
			const std::chrono::milliseconds cacheRpcTimeout)
			: m_logger(std::move(logger))
			, m_sessionRegistry(std::make_shared<AuctionHouseServer::Contents::FAuctionSessionRegistry>())
			, m_redisAuthEnabled(authConfig.enabled)
			, m_itemDataTable(std::move(itemDataTable))
			, m_inventoryPolicyTable(std::move(inventoryPolicyTable))
			, m_mailPolicyTable(std::move(mailPolicyTable))
			, m_timingMetricsRuntime(AuctionHouseServer::Diagnostics::BuildAuctionTimingMetricsConfig(timingMetricsFlushIntervalSeconds))
			, m_timingCsvLogger(m_timingMetricsRuntime, std::move(timingCsvPath))
			, m_databaseConfig(std::move(databaseConfig))
			, m_cacheRpcClient(std::move(cacheRpcClient))
		{
			if (m_cacheRpcClient == nullptr || m_inventoryPolicyTable == nullptr || m_mailPolicyTable == nullptr)
			{
				throw std::invalid_argument("auction application dependency is null.");
			}
			AuctionHouseServer::Diagnostics::ConfigureAuctionDatabaseTiming(m_databaseConfig, m_timingMetricsRuntime);

			ContentsRuntime::Core::SContentRuntimeConfig runtimeConfig{};
			runtimeConfig.workerThreadCount = contentsWorkerThreadCount;
			runtimeConfig.timingMetricsRuntime = &m_timingMetricsRuntime;
			runtimeConfig.packetTimingMetrics = AuctionHouseServer::Diagnostics::BuildAuctionPacketTimingMetrics(m_timingMetricsRuntime);
			m_contentRuntime.SetConfig(runtimeConfig);

			ContentsRuntime::Core::FContentInstanceIdAllocator allocator;
			std::vector<ContentsRuntime::Core::FContentInstanceId> commandShardInstanceIds;
			commandShardInstanceIds.reserve(commandShardCount);
			for (std::uint32_t shardIndex = 0; shardIndex < commandShardCount; ++shardIndex)
			{
				const auto commandContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kCommandContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(commandContentInstanceId))
				{
					throw std::runtime_error("command shard instance id allocation failed.");
				}

				commandShardInstanceIds.push_back(commandContentInstanceId);
				auto commandContent = std::make_unique<AuctionHouseServer::Contents::FAuctionCommandContent>(m_logger,
					commandContentInstanceId,
					shardIndex,
					commandShardCount,
					commandMailboxCapacity,
					testDelayShardIndex,
					testDelayMilliseconds,
					faultInjectionAfterAuctionCommit,
					faultInjectionBidRefundBeforeComplete,
					m_sessionRegistry,
					m_itemDataTable,
					auctionPolicyTable,
					m_inventoryPolicyTable,
					m_mailPolicyTable,
					m_databaseConfig,
					m_cacheRpcClient,
					cacheServerInstanceId,
					cacheRpcTimeout);
				auto* const commandContentPointer = commandContent.get();
				if (!m_contentRuntime.RegisterContent(std::move(commandContent)))
				{
					throw std::runtime_error("command shard registration failed.");
				}
				m_commandContents.emplace(commandContentInstanceId, commandContentPointer);
			}

			const auto routerContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kRouterContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(routerContentInstanceId) ||
				!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionContentRouter>(
					m_logger, routerContentInstanceId, m_sessionRegistry, std::move(commandShardInstanceIds))))
			{
				throw std::runtime_error("auction router registration failed.");
			}

			if (authConfig.enabled)
			{
				auto ticketStore = std::make_shared<Connector::FRedisLoginTicketStore>(std::move(authConfig.ticketStore));
				const auto authContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kAuthContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(authContentInstanceId) ||
					!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionAuthContent>(m_logger,
						authContentInstanceId,
						m_sessionRegistry,
						std::move(ticketStore),
						auctionPolicyTable,
						m_inventoryPolicyTable,
						m_mailPolicyTable,
						m_databaseConfig)))
				{
					throw std::runtime_error("auction auth content registration failed.");
				}
			}

			const auto expirationContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kExpirationContentId);
			auto expirationContent = std::make_unique<AuctionHouseServer::Contents::FAuctionExpirationContent>(m_logger,
				expirationContentInstanceId,
				m_sessionRegistry,
				m_databaseConfig,
				m_cacheRpcClient,
				cacheServerInstanceId,
				cacheRpcTimeout,
				expirationPollMilliseconds);
			m_expirationContent = expirationContent.get();
			m_expirationContentInstanceId = expirationContentInstanceId;
			if (!ContentsRuntime::Core::IsValidContentInstanceId(expirationContentInstanceId) ||
				!m_contentRuntime.RegisterContent(std::move(expirationContent)))
			{
				throw std::runtime_error("auction expiration content registration failed.");
			}

			m_cacheRpcClient->SetResponseCallback(
				[this](const std::uint64_t rpcSessionId, const RpcLib::Protocol::FRpcResponse& response)
				{
					const auto commandIt = m_commandContents.find(response.originContentInstanceId);
					if (commandIt != m_commandContents.end())
					{
						auto* const commandContent = commandIt->second;
						if (!m_contentRuntime.EnqueueCompletionToInstance(response.originContentInstanceId,
								[commandContent, rpcSessionId, response]()
								{
									commandContent->ProcessCacheRpcResponse(rpcSessionId, response);
								}))
						{
							Log(Foundation::ELogLevel::Warn,
								"cache RPC response completion enqueue failed. requestId={} originContentInstanceId={}",
								response.requestId,
								response.originContentInstanceId);
						}
						return;
					}

					if (response.originContentInstanceId == m_expirationContentInstanceId && m_expirationContent != nullptr)
					{
						auto* const expirationContent = m_expirationContent;
						if (!m_contentRuntime.EnqueueCompletionToInstance(response.originContentInstanceId,
								[expirationContent, rpcSessionId, response]()
								{
									expirationContent->ProcessCacheRpcResponse(rpcSessionId, response);
								}))
						{
							Log(Foundation::ELogLevel::Warn,
								"expiration cache RPC response completion enqueue failed. requestId={} originContentInstanceId={}",
								response.requestId,
								response.originContentInstanceId);
						}
						return;
					}

					Log(Foundation::ELogLevel::Warn,
						"cache RPC response has unknown origin. requestId={} originContentInstanceId={}",
						response.requestId,
						response.originContentInstanceId);
				});
			m_cacheRpcClient->SetDisconnectCallback(
				[this](const std::uint64_t rpcSessionId)
				{
					for (const auto& [contentInstanceId, commandContent] : m_commandContents)
					{
						if (!m_contentRuntime.EnqueueCompletionToInstance(contentInstanceId,
								[commandContent, rpcSessionId]()
								{
									commandContent->FailCacheRpcSession(rpcSessionId);
								}))
						{
							Log(Foundation::ELogLevel::Warn,
								"cache RPC disconnect completion enqueue failed. contentInstanceId={}",
								contentInstanceId);
						}
					}
					if (m_expirationContent != nullptr && !m_contentRuntime.EnqueueCompletionToInstance(m_expirationContentInstanceId,
															  [expirationContent = m_expirationContent, rpcSessionId]()
															  {
																  expirationContent->FailCacheRpcSession(rpcSessionId);
															  }))
					{
						Log(Foundation::ELogLevel::Warn,
							"expiration cache RPC disconnect completion enqueue failed. contentInstanceId={}",
							m_expirationContentInstanceId);
					}
				});
			m_cacheRpcClient->SetReadyCallback(
				[this](const std::uint64_t rpcSessionId)
				{
					Log(Foundation::ELogLevel::Info, "cache RPC session ready. rpcSessionId={}", rpcSessionId);
				});
		}

		void OnServerStarted(
			NetworkLib::IServer& server) override
		{
			m_server = &server;
			m_timingCsvLogger.Start();
			m_contentRuntime.Start(server);
			std::string cacheRpcError;
			if (!m_cacheRpcClient->Start(cacheRpcError))
			{
				Log(Foundation::ELogLevel::Error, "cache RPC client start failed: " + cacheRpcError);
			}
			Log(Foundation::ELogLevel::Info, "AuctionHouseServer started.");
		}

		void OnClientConnected(
			const std::uint64_t sessionId) override
		{
			if (!m_sessionRegistry->Add(sessionId) ||
				!m_contentRuntime.EnterSession(sessionId,
					m_redisAuthEnabled ? AuctionHouseServer::Contents::kAuthContentId : AuctionHouseServer::Contents::kRouterContentId))
			{
				m_sessionRegistry->Remove(sessionId);
				if (m_server != nullptr)
				{
					m_server->Disconnect(sessionId);
				}
				Log(Foundation::ELogLevel::Error, "auction session initialization failed.");
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
			m_contentRuntime.LeaveSession(sessionId);
			m_sessionRegistry->Remove(sessionId);
		}

		void OnServerStopped() override
		{
			m_cacheRpcClient->Stop();
			m_contentRuntime.Stop();
			m_server = nullptr;
			m_timingCsvLogger.Stop();
			Log(Foundation::ELogLevel::Info, "AuctionHouseServer stopped.");
		}

	private:
		void Log(
			const Foundation::ELogLevel level,
			const std::string& message) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(level, "AuctionHouseServer", message);
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
				m_logger->Log(level, "AuctionHouseServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		std::shared_ptr<AuctionHouseServer::Contents::FAuctionSessionRegistry> m_sessionRegistry;
		bool m_redisAuthEnabled = false;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		std::shared_ptr<const GameData::InventoryPolicy::FInventoryPolicyTable> m_inventoryPolicyTable;
		std::shared_ptr<const GameData::MailPolicy::FMailPolicyTable> m_mailPolicyTable;
		Foundation::Diagnostics::FTimingMetricsRuntime m_timingMetricsRuntime;
		Foundation::Diagnostics::FTimingCsvLogger m_timingCsvLogger;
		AuctionHouseServer::Database::SAuctionDatabaseConfig m_databaseConfig;
		std::shared_ptr<RpcLib::Client::FOutboundRpcClient> m_cacheRpcClient;
		std::unordered_map<ContentsRuntime::Core::FContentInstanceId, AuctionHouseServer::Contents::FAuctionCommandContent*>
			m_commandContents;
		AuctionHouseServer::Contents::FAuctionExpirationContent* m_expirationContent = nullptr;
		ContentsRuntime::Core::FContentInstanceId m_expirationContentInstanceId = ContentsRuntime::Core::kInvalidContentInstanceId;
		ContentsRuntime::Routing::FContentRuntime m_contentRuntime;
		NetworkLib::IServer* m_server = nullptr;
	};
}

int AuctionHouseServer::Application::RunAuctionHouseServer(
	int argc,
	char* argv[])
{
	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	std::filesystem::path configPath;
	if (!TryResolveConfigPath(argc, argv, executableDirectory, configPath))
	{
		std::cerr << "Usage: AuctionHouseServer [--config PATH]\n";
		return 1;
	}

	Generated::Config::AuctionHouseServer::FAuctionHouseServerConfigDocument configDocument{};
	std::string configError;
	if (!Generated::Config::AuctionHouseServer::FAuctionHouseServerConfigLoader::LoadFromFile(configPath, configDocument, configError))
	{
		std::cerr << "AuctionHouseServer config load failed: " << configError << "\n";
		return 1;
	}

	const auto& applicationConfig = configDocument.AuctionHouseServer;
	const auto& loggingConfig = configDocument.Logging;
	const auto& diagnosticsConfig = configDocument.Diagnostics;
	const auto& authenticationConfig = configDocument.Authentication;
	const auto& cacheConfig = configDocument.CacheRpc;
	const auto& auctionDatabaseConfig = configDocument.AuctionDatabase;
	const auto& debugConfig = configDocument.Debug;
	const std::uint32_t requiredContentWorkerCount = applicationConfig.CommandShardCount + 2u + (authenticationConfig.Enabled ? 1u : 0u);
	if (applicationConfig.WorkerThreadCount <= 0 || applicationConfig.MaxSessionCount <= 0 || applicationConfig.RecvBufferSize <= 0 ||
		applicationConfig.RioSendRingSizeBytes < static_cast<std::uint32_t>(NetworkLib::Packet::Framing::kMaxFramedPacketSizeBytes) ||
		applicationConfig.PageSize == 0 || applicationConfig.PacketKey > 0xFFu || applicationConfig.RunSeconds < 0 ||
		applicationConfig.CommandShardCount == 0 || applicationConfig.ContentsWorkerThreadCount <= 0 ||
		static_cast<std::uint32_t>(applicationConfig.ContentsWorkerThreadCount) < requiredContentWorkerCount ||
		applicationConfig.CommandMailboxCapacity == 0 || applicationConfig.ExpirationPollMilliseconds == 0 ||
		applicationConfig.ExpirationPollMilliseconds > 60000u || diagnosticsConfig.TimingMetricsFlushIntervalSeconds <= 0 ||
		debugConfig.TestDelayShardIndex < -1 ||
		(debugConfig.TestDelayShardIndex >= 0 &&
			static_cast<std::uint32_t>(debugConfig.TestDelayShardIndex) >= applicationConfig.CommandShardCount) ||
		cacheConfig.LocalServerInstanceId == 0 || cacheConfig.RemoteServerInstanceId == 0 || cacheConfig.PacketKey > 0xFFu ||
		cacheConfig.RandomKey > 0xFFu || cacheConfig.HandshakeTimeoutMilliseconds == 0 ||
		cacheConfig.HandshakeTimeoutMilliseconds > 60000u || cacheConfig.ReconnectMilliseconds == 0 ||
		cacheConfig.ReconnectMilliseconds > 60000u || cacheConfig.NetworkWorkerThreadCount == 0 || cacheConfig.RecvScratchBufferSize == 0 ||
		cacheConfig.RecvScratchBufferSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
		authenticationConfig.RedisConnectTimeoutMilliseconds == 0 || auctionDatabaseConfig.ReplicaReconnectCooldownMilliseconds == 0 ||
		auctionDatabaseConfig.ReplicaReconnectCooldownMilliseconds > 3600000u || auctionDatabaseConfig.PrimaryConnectTimeoutSeconds == 0 ||
		(auctionDatabaseConfig.Replica1Enabled && auctionDatabaseConfig.Replica1ConnectTimeoutSeconds == 0) ||
		(auctionDatabaseConfig.Replica2Enabled && auctionDatabaseConfig.Replica2ConnectTimeoutSeconds == 0))
	{
		std::cerr << "AuctionHouseServer config apply failed: one or more numeric values are out of range.\n";
		return 1;
	}

	NetworkLib::Core::SServerConfig serverConfig{};
	serverConfig.backendKind = ToBackendKind(applicationConfig.Backend);
	serverConfig.rioSendDispatchMode = ToRioSendDispatchMode(applicationConfig.RioSendDispatchMode);
	serverConfig.bindIp = applicationConfig.BindIp;
	serverConfig.port = applicationConfig.Port;
	serverConfig.workerThreadCount = static_cast<std::uint32_t>(applicationConfig.WorkerThreadCount);
	serverConfig.maxSessionCount = static_cast<std::uint32_t>(applicationConfig.MaxSessionCount);
	serverConfig.recvBufferSize = static_cast<std::uint32_t>(applicationConfig.RecvBufferSize);
	serverConfig.socketSendBufferBytes = applicationConfig.SocketSendBufferBytes;
	serverConfig.rioSendRingSizeBytes = applicationConfig.RioSendRingSizeBytes;
	serverConfig.enablePageBufferReuse = applicationConfig.EnablePagePool;
	serverConfig.pageBufferSize = applicationConfig.PageSize;
	serverConfig.logConfig.minimumLevel = ToLogLevel(loggingConfig.MinimumLevel);
	serverConfig.logConfig.outputDirectory = ResolveRuntimePath(executableDirectory, loggingConfig.OutputDirectory).string();
	serverConfig.logConfig.consoleEnabled = loggingConfig.ConsoleEnabled;
	serverConfig.logConfig.fileEnabled = loggingConfig.FileEnabled;
	serverConfig.logConfig.includeThreadId = loggingConfig.IncludeThreadId;

	NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
	cipherConfig.packetKey = static_cast<std::uint8_t>(applicationConfig.PacketKey);
	serverConfig.packetCipher = std::make_shared<NetworkLib::Crypto::FDefaultPacketCipher>(cipherConfig);
	serverConfig.packetFramer = std::make_shared<NetworkLib::Packet::Framing::FDefaultPacketFramer>();

	auto logger = std::make_shared<Foundation::FCompositeLogger>();
	logger->AddSink(std::make_shared<Foundation::FConsoleLogger>(serverConfig.logConfig));
	logger->AddSink(std::make_shared<Foundation::FFileLogger>(serverConfig.logConfig));
	serverConfig.logger = logger;
	logger->Log(Foundation::ELogLevel::Info, "AuctionHouseServer", "config loaded. path={}", configPath.string());

	auto itemDataTable = std::make_shared<GameData::Item::FItemDataTable>();
	std::string itemDataError;
	const std::filesystem::path gameDataDirectory = ResolveRuntimePath(executableDirectory, applicationConfig.GameDataDirectory);
	const auto itemDataPath = gameDataDirectory / "Item.yaml";
	if (!itemDataTable->Load(itemDataPath, itemDataError))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "item data load failed: " + itemDataError);
		return 1;
	}
	{
		logger->Log(Foundation::ELogLevel::Info,
			"AuctionHouseServer",
			"item data loaded. count={} path={}",
			itemDataTable->Size(),
			itemDataPath.string());
	}

	auto auctionPolicyTable = std::make_shared<GameData::Auction::FAuctionPolicyTable>();
	std::string auctionPolicyError;
	const auto auctionPolicyPath = gameDataDirectory / "AuctionPolicy.yaml";
	if (!auctionPolicyTable->Load(auctionPolicyPath, auctionPolicyError))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "auction policy load failed: " + auctionPolicyError);
		return 1;
	}
	{
		const auto& policy = auctionPolicyTable->Get();
		if (policy.defaultCurrencyDataId > std::numeric_limits<std::uint16_t>::max())
		{
			logger->Log(Foundation::ELogLevel::Error,
				"AuctionHouseServer",
				"auction policy DefaultCurrencyDataId exceeds the packet currencyId range.");
			return 1;
		}
		logger->Log(Foundation::ELogLevel::Info,
			"AuctionHouseServer",
			"auction policy loaded. maxActiveListings={} searchPageSize={} durationSeconds={}/{} default={} path={}",
			policy.maxActiveListings,
			policy.searchPageSize,
			policy.minimumListingDurationSeconds,
			policy.maximumListingDurationSeconds,
			policy.defaultListingDurationSeconds,
			auctionPolicyPath.string());
	}

	auto inventoryPolicyTable = std::make_shared<GameData::InventoryPolicy::FInventoryPolicyTable>();
	std::string inventoryPolicyError;
	const auto inventoryPolicyPath = gameDataDirectory / "InventoryPolicy.yaml";
	if (!inventoryPolicyTable->Load(inventoryPolicyPath, inventoryPolicyError))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "inventory policy load failed: " + inventoryPolicyError);
		return 1;
	}
	logger->Log(Foundation::ELogLevel::Info,
		"AuctionHouseServer",
		"inventory policy loaded. listPageSize={} path={}",
		inventoryPolicyTable->Get().inventoryListPageSize,
		inventoryPolicyPath.string());

	auto mailPolicyTable = std::make_shared<GameData::MailPolicy::FMailPolicyTable>();
	std::string mailPolicyError;
	const auto mailPolicyPath = gameDataDirectory / "MailPolicy.yaml";
	if (!mailPolicyTable->Load(mailPolicyPath, mailPolicyError))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "mail policy load failed: " + mailPolicyError);
		return 1;
	}
	logger->Log(Foundation::ELogLevel::Info,
		"AuctionHouseServer",
		"mail policy loaded. listPageSize={} path={}",
		mailPolicyTable->Get().mailListPageSize,
		mailPolicyPath.string());

	AuctionHouseServer::Database::SAuctionDatabaseConfig databaseConfig{};
	databaseConfig.enabled = auctionDatabaseConfig.Enabled;
	databaseConfig.replicaReconnectCooldownMilliseconds = auctionDatabaseConfig.ReplicaReconnectCooldownMilliseconds;
	if (auctionDatabaseConfig.Enabled)
	{
		if (auctionDatabaseConfig.Password.empty())
		{
			std::cerr << "AuctionHouseServer config apply failed: AuctionDatabase.Password must not be empty "
						 "when AuctionDatabase.Enabled is true.\n";
			return 1;
		}
		databaseConfig.auctionPrimary = {auctionDatabaseConfig.PrimaryHost,
			auctionDatabaseConfig.PrimaryPort,
			auctionDatabaseConfig.PrimaryUser,
			auctionDatabaseConfig.Password,
			auctionDatabaseConfig.PrimarySchema,
			auctionDatabaseConfig.PrimaryConnectTimeoutSeconds};
		if (auctionDatabaseConfig.Replica1Enabled)
		{
			databaseConfig.auctionReplicas.push_back({auctionDatabaseConfig.Replica1Host,
				auctionDatabaseConfig.Replica1Port,
				auctionDatabaseConfig.Replica1User,
				auctionDatabaseConfig.Password,
				auctionDatabaseConfig.Replica1Schema,
				auctionDatabaseConfig.Replica1ConnectTimeoutSeconds});
		}
		if (auctionDatabaseConfig.Replica2Enabled)
		{
			databaseConfig.auctionReplicas.push_back({auctionDatabaseConfig.Replica2Host,
				auctionDatabaseConfig.Replica2Port,
				auctionDatabaseConfig.Replica2User,
				auctionDatabaseConfig.Password,
				auctionDatabaseConfig.Replica2Schema,
				auctionDatabaseConfig.Replica2ConnectTimeoutSeconds});
		}
	}

	SAuctionAuthRuntimeConfig authConfig{};
	authConfig.enabled = authenticationConfig.Enabled;
	authConfig.ticketStore.connection.host = authenticationConfig.RedisHost;
	authConfig.ticketStore.connection.port = authenticationConfig.RedisPort;
	authConfig.ticketStore.connection.password = authenticationConfig.RedisPassword;
	authConfig.ticketStore.connection.database = authenticationConfig.RedisDatabase;
	authConfig.ticketStore.connection.connectTimeoutMs = authenticationConfig.RedisConnectTimeoutMilliseconds;
	authConfig.ticketStore.keyPrefix = authenticationConfig.TicketKeyPrefix;
	authConfig.ticketStore.activeLoginKeyPrefix = authenticationConfig.ActiveLoginKeyPrefix;

	RpcLib::Client::FOutboundRpcClientConfig cacheRpcConfig;
	cacheRpcConfig.host = cacheConfig.Host;
	cacheRpcConfig.port = cacheConfig.Port;
	cacheRpcConfig.localServerType = RpcLib::Protocol::ERpcServerType::Auction;
	cacheRpcConfig.localServerInstanceId = cacheConfig.LocalServerInstanceId;
	cacheRpcConfig.expectedRemoteServerType = RpcLib::Protocol::ERpcServerType::Cache;
	cacheRpcConfig.expectedRemoteServerInstanceId = cacheConfig.RemoteServerInstanceId;
	cacheRpcConfig.packetKey = static_cast<std::uint8_t>(cacheConfig.PacketKey);
	cacheRpcConfig.randomKey = static_cast<std::uint8_t>(cacheConfig.RandomKey);
	cacheRpcConfig.handshakeTimeout = std::chrono::milliseconds(cacheConfig.HandshakeTimeoutMilliseconds);
	cacheRpcConfig.reconnectInterval = std::chrono::milliseconds(cacheConfig.ReconnectMilliseconds);
	cacheRpcConfig.networkWorkerThreadCount = cacheConfig.NetworkWorkerThreadCount;
	cacheRpcConfig.recvScratchBufferSize = static_cast<std::size_t>(cacheConfig.RecvScratchBufferSize);
	auto cacheRpcClient = std::make_shared<RpcLib::Client::FOutboundRpcClient>(std::move(cacheRpcConfig));

	const std::uint32_t testDelayShardIndex = debugConfig.TestDelayShardIndex < 0
												  ? std::numeric_limits<std::uint32_t>::max()
												  : static_cast<std::uint32_t>(debugConfig.TestDelayShardIndex);
	FAuctionApplication application(logger,
		applicationConfig.CommandShardCount,
		applicationConfig.ContentsWorkerThreadCount,
		applicationConfig.CommandMailboxCapacity,
		testDelayShardIndex,
		debugConfig.TestDelayMilliseconds,
		debugConfig.FaultInjectListingRegisterAfterAuctionCommit,
		debugConfig.FaultInjectBidRefundBeforeComplete,
		applicationConfig.ExpirationPollMilliseconds,
		std::move(authConfig),
		diagnosticsConfig.TimingMetricsFlushIntervalSeconds,
		ResolveRuntimePath(executableDirectory, diagnosticsConfig.TimingCsvPath).string(),
		std::move(itemDataTable),
		std::move(auctionPolicyTable),
		std::move(inventoryPolicyTable),
		std::move(mailPolicyTable),
		std::move(databaseConfig),
		std::move(cacheRpcClient),
		cacheConfig.RemoteServerInstanceId,
		std::chrono::milliseconds(cacheConfig.HandshakeTimeoutMilliseconds));
	std::unique_ptr<NetworkLib::IServer> server = NetworkLib::Core::FServerFactory::Create(serverConfig.backendKind);
	if (server == nullptr || !server->Start(serverConfig, application))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "server start failed.");
		return 1;
	}

	std::cout << "AuctionHouseServer listening on " << serverConfig.bindIp << ":" << serverConfig.port << "\n";
	if (applicationConfig.RunSeconds > 0)
	{
		std::this_thread::sleep_for(std::chrono::seconds(applicationConfig.RunSeconds));
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
