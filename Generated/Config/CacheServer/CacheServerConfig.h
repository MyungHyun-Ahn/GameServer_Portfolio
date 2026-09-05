#pragma once

namespace Generated::Config::CacheServer
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

	enum class ELogMinimumLevel
	{
		Debug,
		Info,
		Warn,
		Error
	};

	struct SCacheServerConfig
	{
		EBackend Backend = EBackend::Iocp;
		ERioSendDispatchMode RioSendDispatchMode = ERioSendDispatchMode::Direct;
		std::string BindIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19103);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(2);
		std::int32_t MaxSessionCount = static_cast<std::int32_t>(128);
		std::int32_t RecvBufferSize = static_cast<std::int32_t>(65536);
		std::int32_t SocketSendBufferBytes = static_cast<std::int32_t>(-1);
		std::uint32_t RioSendRingSizeBytes = static_cast<std::uint32_t>(65536);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::int32_t ContentsWorkerThreadCount = static_cast<std::int32_t>(5);
		std::uint32_t PlayerCacheShardCount = static_cast<std::uint32_t>(4);
		std::uint64_t MailboxCapacity = static_cast<std::uint64_t>(1024);
		std::uint32_t RpcServerInstanceId = static_cast<std::uint32_t>(1);
		std::string GameDataDirectory = "GameData";
		bool DatabaseEnabled = false;
		std::uint32_t ReplicaReconnectCooldownMilliseconds = static_cast<std::uint32_t>(60000);
		ELogMinimumLevel LogMinimumLevel = ELogMinimumLevel::Info;
		std::string LogOutputDirectory = "";
		bool LogConsoleEnabled = true;
		bool LogFileEnabled = true;
		bool LogIncludeThreadId = true;
	};

	struct SCacheServerGameDatabaseConfig
	{
		std::string Password = "";
	};

	struct SCacheServerGamePrimaryConfig
	{
		std::string Host = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(3310);
		std::string User = "appuser";
		std::string Database = "gamedb";
		std::uint32_t ConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
	};

	struct SCacheServerGameReplica1Config
	{
		bool Enabled = true;
		std::string Host = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(3311);
		std::string User = "appuser";
		std::string Database = "gamedb";
		std::uint32_t ConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
	};

	struct SCacheServerGameReplica2Config
	{
		bool Enabled = true;
		std::string Host = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(3312);
		std::string User = "appuser";
		std::string Database = "gamedb";
		std::uint32_t ConnectTimeoutSeconds = static_cast<std::uint32_t>(3);
	};

	struct SCacheServerCachePolicyConfig
	{
		std::uint32_t GameOwnerLeaseMilliseconds = static_cast<std::uint32_t>(30000);
		std::uint32_t IdleEvictionMilliseconds = static_cast<std::uint32_t>(300000);
		std::uint32_t MaintenanceIntervalMilliseconds = static_cast<std::uint32_t>(1000);
		std::uint32_t RevokeTimeoutMilliseconds = static_cast<std::uint32_t>(2000);
	};

	struct SCacheServerFaultInjectionConfig
	{
		bool CreditBeforeDatabaseTransaction = false;
		bool CreditAfterCommitDisconnect = false;
		std::uint32_t CreditBeforeDatabaseDelayMilliseconds = static_cast<std::uint32_t>(0);
		std::uint32_t CreditAfterCommitDelayMilliseconds = static_cast<std::uint32_t>(0);
	};

	struct SCacheServerDebugConfig
	{
		std::uint32_t RunSeconds = static_cast<std::uint32_t>(0);
		bool Headless = false;
	};

	struct FCacheServerConfigDocument
	{
		SCacheServerConfig CacheServer;
		SCacheServerGameDatabaseConfig GameDatabase;
		SCacheServerGamePrimaryConfig GamePrimary;
		SCacheServerGameReplica1Config GameReplica1;
		SCacheServerGameReplica2Config GameReplica2;
		SCacheServerCachePolicyConfig CachePolicy;
		SCacheServerFaultInjectionConfig FaultInjection;
		SCacheServerDebugConfig Debug;
	};

	class FCacheServerConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath, FCacheServerConfigDocument& outConfig, std::string& outError);
	};
}
