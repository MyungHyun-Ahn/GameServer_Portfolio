#pragma once

namespace Generated::Config::WorldServer
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

	enum class EAuthMode
	{
		Disabled,
		Redis
	};

	enum class ELogMinimumLevel
	{
		Debug,
		Info,
		Warn,
		Error
	};

	struct SWorldServerConfig
	{
		EBackend Backend = EBackend::Iocp;
		ERioSendDispatchMode RioSendDispatchMode = ERioSendDispatchMode::OwnerThread;
		std::string BindIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19200);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(2);
		std::int32_t MaxSessionCount = static_cast<std::int32_t>(1024);
		std::int32_t RecvBufferSize = static_cast<std::int32_t>(4096);
		std::int32_t SocketSendBufferBytes = static_cast<std::int32_t>(-1);
		std::uint32_t RioSendRingSizeBytes = static_cast<std::uint32_t>(65536);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::int32_t ContentsWorkerThreadCount = static_cast<std::int32_t>(4);
		std::int32_t SectorExecutorInstanceCount = static_cast<std::int32_t>(4);
		std::uint32_t SectorTaskPumpBatchSize = static_cast<std::uint32_t>(16);
		std::int32_t MapContentShardCount = static_cast<std::int32_t>(4);
		std::uint32_t MapTickFps = static_cast<std::uint32_t>(20);
		float MovementCorrectionTolerance = 64.0;
		std::string GameDataDirectory = "GameData";
		EAuthMode AuthMode = EAuthMode::Disabled;
		std::string LoginRedisHost = "127.0.0.1";
		std::uint16_t LoginRedisPort = static_cast<std::uint16_t>(6379);
		std::string LoginRedisPassword = "";
		std::int32_t LoginRedisDatabase = static_cast<std::int32_t>(0);
		std::uint32_t LoginRedisConnectTimeoutMilliseconds = static_cast<std::uint32_t>(3000);
		std::string WorldTicketKeyPrefix = "world:ticket:";
		std::string ActiveLoginKeyPrefix = "chat:active-login:";
		bool CacheEnabled = false;
		std::string CacheHost = "127.0.0.1";
		std::uint16_t CachePort = static_cast<std::uint16_t>(19103);
		std::uint32_t CacheRpcPacketKey = static_cast<std::uint32_t>(55);
		std::uint32_t RpcServerInstanceId = static_cast<std::uint32_t>(1);
		std::uint32_t CacheServerInstanceId = static_cast<std::uint32_t>(1);
		std::uint32_t CacheRpcTimeoutMilliseconds = static_cast<std::uint32_t>(3000);
		std::uint32_t CacheReconnectMilliseconds = static_cast<std::uint32_t>(1000);
		ELogMinimumLevel LogMinimumLevel = ELogMinimumLevel::Info;
		std::string LogOutputDirectory = "";
		bool LogConsoleEnabled = true;
		bool LogFileEnabled = true;
		bool LogIncludeThreadId = true;
	};

	struct SWorldServerDebugConfig
	{
		bool Headless = false;
	};

	struct FWorldServerConfigDocument
	{
		SWorldServerConfig WorldServer;
		SWorldServerDebugConfig Debug;
	};

	class FWorldServerConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath, FWorldServerConfigDocument& outConfig, std::string& outError);
	};
}
