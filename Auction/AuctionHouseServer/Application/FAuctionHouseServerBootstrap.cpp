#include "AuctionHouseServerPch.h"

#include "AuctionHouseServer/Application/FAuctionHouseServerBootstrap.h"

#include "AuctionHouseServer/Contents/Command/FAuctionCommandContent.h"
#include "AuctionHouseServer/Contents/Auth/FAuctionAuthContent.h"
#include "AuctionHouseServer/Contents/ContentTypes.h"
#include "AuctionHouseServer/Contents/FAuctionContentRouter.h"
#include "AuctionHouseServer/Contents/Expiration/FAuctionExpirationContent.h"
#include "AuctionHouseServer/Contents/Session/FAuctionUserRegistry.h"
#include "AuctionHouseServer/Diagnostics/FAuctionTimingSetup.h"
#include "Connector/Config/RedisChatTicketStoreTypes.h"
#include "Connector/Redis/FRedisChatTicketStore.h"
#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "Foundation/Logging/FCompositeLogger.h"
#include "Foundation/Logging/FConsoleLogger.h"
#include "Foundation/Logging/FFileLogger.h"
#include "Generated/Packets/Auction/AuctionPackets.h"
#include "GameData/Auction/FAuctionPolicyTable.h"
#include "GameData/Item/FItemDataTable.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FServerFactory.h"
#include "Servers/IApplicationHandler.h"

#include <cstdlib>
#include <limits>
#include <stdexcept>

#include <format>
namespace
{
	constexpr std::uint8_t kPacketKey = 0x37;
	constexpr std::uint16_t kDefaultPort = 19102;
	constexpr std::uint64_t kDefaultCommandMailboxCapacity = 1024;
	constexpr int kTimingMetricsFlushIntervalSeconds = 60;

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

	struct SCommandLineOptions
	{
		std::uint16_t port = kDefaultPort;
		int runSeconds = 0;
		std::uint64_t commandMailboxCapacity = kDefaultCommandMailboxCapacity;
		std::uint32_t testDelayShardIndex = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t testDelayMilliseconds = 0;
		std::uint32_t expirationPollMilliseconds = 5000;
		std::uint32_t replicaReconnectCooldownMilliseconds = 60000;
		bool faultInjectionAfterAuctionCommit = false;
		bool databaseEnabled = false;
		bool redisAuthEnabled = false;
	};

	bool TryParseOptions(
		const int argc,
		char* argv[],
		SCommandLineOptions& outOptions)
	{
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index];
			if (argument == "--port" && index + 1 < argc)
			{
				const int port = std::atoi(argv[++index]);
				if (port <= 0 || port > 65535)
				{
					return false;
				}
				outOptions.port = static_cast<std::uint16_t>(port);
			}
			else if (argument == "--run-seconds" && index + 1 < argc)
			{
				outOptions.runSeconds = std::max(0, std::atoi(argv[++index]));
			}
			else if (argument == "--command-mailbox-capacity" && index + 1 < argc)
			{
				const unsigned long long capacity = std::strtoull(argv[++index], nullptr, 10);
				if (capacity == 0)
				{
					return false;
				}
				outOptions.commandMailboxCapacity = static_cast<std::uint64_t>(capacity);
			}
			else if (argument == "--test-delay-shard" && index + 1 < argc)
			{
				const int shardIndex = std::atoi(argv[++index]);
				if (shardIndex < 0 || shardIndex >= static_cast<int>(AuctionHouseServer::Contents::kCommandShardCount))
				{
					return false;
				}
				outOptions.testDelayShardIndex = static_cast<std::uint32_t>(shardIndex);
			}
			else if (argument == "--test-delay-ms" && index + 1 < argc)
			{
				const int delayMilliseconds = std::atoi(argv[++index]);
				if (delayMilliseconds < 0)
				{
					return false;
				}
				outOptions.testDelayMilliseconds = static_cast<std::uint32_t>(delayMilliseconds);
			}
			else if (argument == "--expiration-poll-ms" && index + 1 < argc)
			{
				const int pollMilliseconds = std::atoi(argv[++index]);
				if (pollMilliseconds <= 0 || pollMilliseconds > 60000)
					return false;
				outOptions.expirationPollMilliseconds = static_cast<std::uint32_t>(pollMilliseconds);
			}
			else if (argument == "--replica-reconnect-cooldown-ms" && index + 1 < argc)
			{
				const int cooldownMilliseconds = std::atoi(argv[++index]);
				if (cooldownMilliseconds <= 0 || cooldownMilliseconds > 3600000)
					return false;
				outOptions.replicaReconnectCooldownMilliseconds = static_cast<std::uint32_t>(cooldownMilliseconds);
			}
			else if (argument == "--fault-inject-listing-register-after-auction-commit")
			{
				outOptions.faultInjectionAfterAuctionCommit = true;
			}
			else if (argument == "--database-enabled")
			{
				outOptions.databaseEnabled = true;
			}
			else if (argument == "--redis-auth-enabled")
			{
				outOptions.redisAuthEnabled = true;
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	class FAuctionApplication final : public NetworkLib::IApplicationHandler
	{
	public:
		FAuctionApplication(
			std::shared_ptr<Foundation::ILogger> logger,
			const std::uint64_t commandMailboxCapacity,
			const std::uint32_t testDelayShardIndex,
			const std::uint32_t testDelayMilliseconds,
			const bool faultInjectionAfterAuctionCommit,
			const std::uint32_t expirationPollMilliseconds,
			const bool redisAuthEnabled,
			std::shared_ptr<const GameData::Item::FItemDataTable> itemDataTable,
			std::shared_ptr<const GameData::Auction::FAuctionPolicyTable> auctionPolicyTable,
			AuctionHouseServer::Database::SAuctionDatabaseConfig databaseConfig)
			: m_logger(std::move(logger))
			, m_userRegistry(std::make_shared<AuctionHouseServer::Contents::FAuctionUserRegistry>())
			, m_redisAuthEnabled(redisAuthEnabled)
			, m_itemDataTable(std::move(itemDataTable))
			, m_timingMetricsRuntime(AuctionHouseServer::Diagnostics::BuildAuctionTimingMetricsConfig(kTimingMetricsFlushIntervalSeconds))
			, m_timingCsvLogger(m_timingMetricsRuntime, (GetExecutableDirectory() / "logs" / "auction_timing.csv").string())
			, m_databaseConfig(std::move(databaseConfig))
		{
			AuctionHouseServer::Diagnostics::ConfigureAuctionDatabaseTiming(m_databaseConfig, m_timingMetricsRuntime);

			ContentsRuntime::Core::SContentRuntimeConfig runtimeConfig{};
			runtimeConfig.workerThreadCount = AuctionHouseServer::Contents::kCommandShardCount + 2 + (redisAuthEnabled ? 1 : 0);
			runtimeConfig.timingMetricsRuntime = &m_timingMetricsRuntime;
			runtimeConfig.packetTimingMetrics = AuctionHouseServer::Diagnostics::BuildAuctionPacketTimingMetrics(m_timingMetricsRuntime);
			m_contentRuntime.SetConfig(runtimeConfig);

			ContentsRuntime::Core::FContentInstanceIdAllocator allocator;
			std::vector<ContentsRuntime::Core::FContentInstanceId> commandShardInstanceIds;
			commandShardInstanceIds.reserve(AuctionHouseServer::Contents::kCommandShardCount);
			for (std::uint32_t shardIndex = 0; shardIndex < AuctionHouseServer::Contents::kCommandShardCount; ++shardIndex)
			{
				const auto commandContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kCommandContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(commandContentInstanceId))
				{
					throw std::runtime_error("command shard instance id allocation failed.");
				}

				commandShardInstanceIds.push_back(commandContentInstanceId);
				if (!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionCommandContent>(m_logger,
						commandContentInstanceId,
						shardIndex,
						AuctionHouseServer::Contents::kCommandShardCount,
						commandMailboxCapacity,
						testDelayShardIndex,
						testDelayMilliseconds,
						faultInjectionAfterAuctionCommit,
						m_userRegistry,
						m_itemDataTable,
						auctionPolicyTable,
						m_databaseConfig)))
				{
					throw std::runtime_error("command shard registration failed.");
				}
			}

			const auto routerContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kRouterContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(routerContentInstanceId) ||
				!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionContentRouter>(
					m_logger, routerContentInstanceId, m_userRegistry, std::move(commandShardInstanceIds))))
			{
				throw std::runtime_error("auction router registration failed.");
			}

			if (redisAuthEnabled)
			{
				Connector::SRedisLoginTicketStoreConfig ticketConfig{};
				ticketConfig.connection.host = "127.0.0.1";
				ticketConfig.connection.port = 6379;
				ticketConfig.keyPrefix = "auction:ticket:";
				ticketConfig.activeLoginKeyPrefix = "chat:active-login:";
				auto ticketStore = std::make_shared<Connector::FRedisLoginTicketStore>(std::move(ticketConfig));
				const auto authContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kAuthContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(authContentInstanceId) ||
					!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionAuthContent>(
						m_logger, authContentInstanceId, m_userRegistry, std::move(ticketStore), auctionPolicyTable, m_databaseConfig)))
				{
					throw std::runtime_error("auction auth content registration failed.");
				}
			}

			const auto expirationContentInstanceId = allocator.Allocate(AuctionHouseServer::Contents::kExpirationContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(expirationContentInstanceId) ||
				!m_contentRuntime.RegisterContent(std::make_unique<AuctionHouseServer::Contents::FAuctionExpirationContent>(
					m_logger, expirationContentInstanceId, m_userRegistry, m_databaseConfig, expirationPollMilliseconds)))
			{
				throw std::runtime_error("auction expiration content registration failed.");
			}
		}

		void OnServerStarted(
			NetworkLib::IServer& server) override
		{
			m_timingCsvLogger.Start();
			m_contentRuntime.Start(server);
			Log(Foundation::ELogLevel::Info, "AuctionHouseServer started.");
		}

		void OnClientConnected(
			const std::uint64_t sessionId) override
		{
			m_contentRuntime.EnterSession(sessionId,
				m_redisAuthEnabled ? AuctionHouseServer::Contents::kAuthContentId : AuctionHouseServer::Contents::kRouterContentId);
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
			m_userRegistry->Remove(sessionId);
		}

		void OnServerStopped() override
		{
			m_contentRuntime.Stop();
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
		std::shared_ptr<AuctionHouseServer::Contents::FAuctionUserRegistry> m_userRegistry;
		bool m_redisAuthEnabled = false;
		std::shared_ptr<const GameData::Item::FItemDataTable> m_itemDataTable;
		Foundation::Diagnostics::FTimingMetricsRuntime m_timingMetricsRuntime;
		Foundation::Diagnostics::FTimingCsvLogger m_timingCsvLogger;
		AuctionHouseServer::Database::SAuctionDatabaseConfig m_databaseConfig;
		ContentsRuntime::Routing::FContentRuntime m_contentRuntime;
	};
}

int AuctionHouseServer::Application::RunAuctionHouseServer(
	int argc,
	char* argv[])
{
	SCommandLineOptions options{};
	if (!TryParseOptions(argc, argv, options))
	{
		std::cerr << "Usage: AuctionHouseServer [--port N] [--run-seconds N]"
				  << " [--command-mailbox-capacity N] [--test-delay-shard N] [--test-delay-ms N]"
				  << " [--expiration-poll-ms N]"
				  << " [--replica-reconnect-cooldown-ms N]"
				  << " [--fault-inject-listing-register-after-auction-commit]"
				  << " [--database-enabled] [--redis-auth-enabled]\n";
		return 1;
	}

	NetworkLib::Core::SServerConfig serverConfig{};
	serverConfig.backendKind = NetworkLib::Core::EBackendKind::Iocp;
	serverConfig.bindIp = "127.0.0.1";
	serverConfig.port = options.port;
	serverConfig.workerThreadCount = 2;
	serverConfig.maxSessionCount = 128;
	serverConfig.recvBufferSize = 1024;
	serverConfig.logConfig.outputDirectory = "logs";
	serverConfig.logConfig.consoleEnabled = true;
	serverConfig.logConfig.fileEnabled = true;

	NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
	cipherConfig.packetKey = kPacketKey;
	serverConfig.packetCipher = std::make_shared<NetworkLib::Crypto::FDefaultPacketCipher>(cipherConfig);
	serverConfig.packetFramer = std::make_shared<NetworkLib::Packet::Framing::FDefaultPacketFramer>();

	auto logger = std::make_shared<Foundation::FCompositeLogger>();
	logger->AddSink(std::make_shared<Foundation::FConsoleLogger>(serverConfig.logConfig));
	logger->AddSink(std::make_shared<Foundation::FFileLogger>(serverConfig.logConfig));
	serverConfig.logger = logger;

	auto itemDataTable = std::make_shared<GameData::Item::FItemDataTable>();
	std::string itemDataError;
	const auto itemDataPath = GetExecutableDirectory() / "Config" / "GameData" / "Items.yaml";
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
	const auto auctionPolicyPath = GetExecutableDirectory() / "Config" / "GameData" / "Auction.yaml";
	if (!auctionPolicyTable->Load(auctionPolicyPath, auctionPolicyError))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "auction policy load failed: " + auctionPolicyError);
		return 1;
	}
	{
		const auto& policy = auctionPolicyTable->Get();
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

	AuctionHouseServer::Database::SAuctionDatabaseConfig databaseConfig{};
	databaseConfig.enabled = options.databaseEnabled;
	databaseConfig.replicaReconnectCooldownMilliseconds = options.replicaReconnectCooldownMilliseconds;
	if (options.databaseEnabled)
	{
		char* passwordBuffer = nullptr;
		std::size_t passwordLength = 0;
		if (_dupenv_s(&passwordBuffer, &passwordLength, "MYSQL_PASSWORD") != 0 || passwordBuffer == nullptr || passwordLength <= 1)
		{
			std::free(passwordBuffer);
			std::cerr << "MYSQL_PASSWORD must be set when --database-enabled is used.\n";
			return 1;
		}
		const std::string password(passwordBuffer);
		std::free(passwordBuffer);
		databaseConfig.gamePrimary = {"127.0.0.1", 3310, "appuser", password, "gamedb", 3};
		databaseConfig.gameReplicas = {
			{"127.0.0.1", 3311, "appuser", password, "gamedb", 3}, {"127.0.0.1", 3312, "appuser", password, "gamedb", 3}};
		databaseConfig.auctionPrimary = {"127.0.0.1", 3320, "appuser", password, "auctiondb", 3};
		databaseConfig.auctionReplicas = {
			{"127.0.0.1", 3321, "appuser", password, "auctiondb", 3}, {"127.0.0.1", 3322, "appuser", password, "auctiondb", 3}};
	}

	FAuctionApplication application(logger,
		options.commandMailboxCapacity,
		options.testDelayShardIndex,
		options.testDelayMilliseconds,
		options.faultInjectionAfterAuctionCommit,
		options.expirationPollMilliseconds,
		options.redisAuthEnabled,
		std::move(itemDataTable),
		std::move(auctionPolicyTable),
		std::move(databaseConfig));
	std::unique_ptr<NetworkLib::IServer> server = NetworkLib::Core::FServerFactory::Create(serverConfig.backendKind);
	if (server == nullptr || !server->Start(serverConfig, application))
	{
		logger->Log(Foundation::ELogLevel::Error, "AuctionHouseServer", "server start failed.");
		return 1;
	}

	std::cout << "AuctionHouseServer listening on 127.0.0.1:" << options.port << "\n";
	if (options.runSeconds > 0)
	{
		std::this_thread::sleep_for(std::chrono::seconds(options.runSeconds));
	}
	else
	{
		std::cout << "Press ENTER to stop.\n";
		std::cin.get();
	}

	server->Stop();
	return 0;
}
