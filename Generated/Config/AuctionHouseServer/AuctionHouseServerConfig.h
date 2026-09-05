#pragma once

namespace Generated::Config::AuctionHouseServer
{
	enum class EBackend
	{
		Iocp,
		Rio
	};

	enum class ERioSendDispatchMode
	{
		Direct,
		OwnerThread
	};

	enum class ELoggingMinimumLevel
	{
		Debug,
		Info,
		Warn,
		Error
	};

	struct SAuctionHouseServerConfig
	{
		EBackend Backend = EBackend::Iocp;
		ERioSendDispatchMode RioSendDispatchMode = ERioSendDispatchMode::Direct;
		std::string BindIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19102);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(2);
		std::int32_t MaxSessionCount = static_cast<std::int32_t>(128);
		std::int32_t RecvBufferSize = static_cast<std::int32_t>(1024);
		std::int32_t SocketSendBufferBytes = static_cast<std::int32_t>(-1);
		std::uint32_t RioSendRingSizeBytes = static_cast<std::uint32_t>(65536);
		bool EnablePagePool = true;
		std::uint32_t PageSize = static_cast<std::uint32_t>(4096);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::int32_t RunSeconds = static_cast<std::int32_t>(0);
		std::uint32_t CommandShardCount = static_cast<std::uint32_t>(4);
		std::int32_t ContentsWorkerThreadCount = static_cast<std::int32_t>(7);
		std::uint64_t CommandMailboxCapacity = static_cast<std::uint64_t>(1024);
		std::uint32_t ExpirationPollMilliseconds = static_cast<std::uint32_t>(5000);
		std::string GameDataDirectory = "Config/GameData";
	};

	struct SAuctionHouseServerLoggingConfig
	{
		ELoggingMinimumLevel MinimumLevel = ELoggingMinimumLevel::Info;
		std::string OutputDirectory = "logs";
		bool ConsoleEnabled = true;
		bool FileEnabled = true;
		bool IncludeThreadId = true;
	};

	struct SAuctionHouseServerDiagnosticsConfig
	{
		std::int32_t TimingMetricsFlushIntervalSeconds = static_cast<std::int32_t>(60);
		std::string TimingCsvPath = "logs/auction_timing.csv";
	};

	struct SAuctionHouseServerAuthenticationConfig
	{
		bool Enabled = false;
		std::string RedisHost = "127.0.0.1";
		std::uint16_t RedisPort = static_cast<std::uint16_t>(6379);
		std::string RedisPassword = "";
		std::int32_t RedisDatabase = static_cast<std::int32_t>(0);
		std::uint32_t RedisConnectTimeoutMilliseconds = static_cast<std::uint32_t>(3000);
		std::string TicketKeyPrefix = "auction:ticket:";
		std::string ActiveLoginKeyPrefix = "chat:active-login:";
	};

	struct SAuctionHouseServerCacheRpcConfig
	{
		std::string Host = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19103);
		std::uint32_t LocalServerInstanceId = static_cast<std::uint32_t>(1);
		std::uint32_t RemoteServerInstanceId = static_cast<std::uint32_t>(1);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::uint32_t RandomKey = static_cast<std::uint32_t>(81);
		std::uint32_t HandshakeTimeoutMilliseconds = static_cast<std::uint32_t>(3000);
		std::uint32_t ReconnectMilliseconds = static_cast<std::uint32_t>(1000);
		std::uint32_t NetworkWorkerThreadCount = static_cast<std::uint32_t>(2);
		std::uint64_t RecvScratchBufferSize = static_cast<std::uint64_t>(65536);
	};

	struct SAuctionHouseServerAuctionDatabaseConfig
	{
		bool Enabled = false;
		std::string Password = "";
		std::uint32_t ReplicaReconnectCooldownMilliseconds = static_cast<std::uint32_t>(60000);
		std::string PrimaryHost = "127.0.0.1";
		std::uint16_t PrimaryPort = static_cast<std::uint16_t>(3320);
		std::string PrimaryUser = "appuser";
		std::string PrimarySchema = "auctiondb";
		std::uint32_t PrimaryConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
		bool Replica1Enabled = true;
		std::string Replica1Host = "127.0.0.1";
		std::uint16_t Replica1Port = static_cast<std::uint16_t>(3321);
		std::string Replica1User = "appuser";
		std::string Replica1Schema = "auctiondb";
		std::uint32_t Replica1ConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
		bool Replica2Enabled = true;
		std::string Replica2Host = "127.0.0.1";
		std::uint16_t Replica2Port = static_cast<std::uint16_t>(3322);
		std::string Replica2User = "appuser";
		std::string Replica2Schema = "auctiondb";
		std::uint32_t Replica2ConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
	};

	struct SAuctionHouseServerDebugConfig
	{
		std::int32_t TestDelayShardIndex = static_cast<std::int32_t>(-1);
		std::uint32_t TestDelayMilliseconds = static_cast<std::uint32_t>(0);
		bool FaultInjectListingRegisterAfterAuctionCommit = false;
		bool FaultInjectBidRefundBeforeComplete = false;
	};

	struct FAuctionHouseServerConfigDocument
	{
		SAuctionHouseServerConfig AuctionHouseServer;
		SAuctionHouseServerLoggingConfig Logging;
		SAuctionHouseServerDiagnosticsConfig Diagnostics;
		SAuctionHouseServerAuthenticationConfig Authentication;
		SAuctionHouseServerCacheRpcConfig CacheRpc;
		SAuctionHouseServerAuctionDatabaseConfig AuctionDatabase;
		SAuctionHouseServerDebugConfig Debug;
	};

	class FAuctionHouseServerConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath,
			FAuctionHouseServerConfigDocument& outConfig,
			std::string& outError);
	};
}
