#pragma once

namespace Generated::Config::ChattingServer
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

	enum class EDebugTransitionRaceInjectionMode
	{
		None,
		SwitchToThread,
		Sleep0,
		Yield
	};

	enum class EDebugPostRoomChangeRaceInjectionMode
	{
		None,
		SwitchToThread,
		Sleep0,
		Yield
	};

	enum class EDebugContentsRaceInjectionMode
	{
		None,
		SwitchToThread,
		Sleep0,
		Yield
	};

	enum class ELoginAuthMode
	{
		Disabled,
		Redis
	};

	struct SChattingServerConfig
	{
		EBackend Backend = EBackend::Iocp;
		ERioSendDispatchMode RioSendDispatchMode = ERioSendDispatchMode::Direct;
		std::string BindIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19100);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(2);
		std::int32_t MaxSessionCount = static_cast<std::int32_t>(512);
		std::int32_t RecvBufferSize = static_cast<std::int32_t>(1024);
		std::int32_t SocketSendBufferBytes = static_cast<std::int32_t>(-1);
		std::uint32_t RioSendRingSizeBytes = static_cast<std::uint32_t>(65536);
		ELogMinimumLevel LogMinimumLevel = ELogMinimumLevel::Info;
		std::string LogOutputDirectory = "";
		bool LogConsoleEnabled = true;
		bool LogFileEnabled = true;
		bool LogIncludeThreadId = true;
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		bool EnablePagePool = true;
		std::uint32_t PageSize = static_cast<std::uint32_t>(4096);
		std::int32_t ContentsWorkerThreadCount = static_cast<std::int32_t>(4);
		std::int32_t RoomCount = static_cast<std::int32_t>(50);
		std::int32_t RoomCapacity = static_cast<std::int32_t>(8);
		std::int32_t MaxChatPayloadBytes = static_cast<std::int32_t>(8000);
	};

	struct SChattingServerDebugConfig
	{
		bool ManualDump = false;
		bool Headless = false;
		bool BootstrapTrace = false;
		std::uint32_t TraceUserId = static_cast<std::uint32_t>(0);
		bool LogPackets = false;
		bool TransitionRaceInjectionEnabled = false;
		EDebugTransitionRaceInjectionMode TransitionRaceInjectionMode = EDebugTransitionRaceInjectionMode::None;
		bool PostRoomChangeRaceInjectionEnabled = false;
		EDebugPostRoomChangeRaceInjectionMode PostRoomChangeRaceInjectionMode = EDebugPostRoomChangeRaceInjectionMode::None;
		bool ContentsRaceInjectionEnabled = false;
		std::uint32_t ContentsRaceInjectionPeriod = static_cast<std::uint32_t>(100);
		EDebugContentsRaceInjectionMode ContentsRaceInjectionMode = EDebugContentsRaceInjectionMode::None;
		bool ContentsFailFast = false;
	};

	struct SChattingServerLoginAuthConfig
	{
		ELoginAuthMode Mode = ELoginAuthMode::Redis;
		std::string RedisHost = "127.0.0.1";
		std::uint16_t RedisPort = static_cast<std::uint16_t>(6379);
		std::string RedisPassword = "";
		std::int32_t RedisDatabase = static_cast<std::int32_t>(0);
		std::uint32_t RedisConnectTimeoutMs = static_cast<std::uint32_t>(3000);
		std::string RedisKeyPrefix = "chat:ticket:";
	};

	struct FChattingServerConfigDocument
	{
		SChattingServerConfig ChattingServer;
		SChattingServerDebugConfig Debug;
		SChattingServerLoginAuthConfig LoginAuth;
	};

	class FChattingServerConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath, FChattingServerConfigDocument& outConfig, std::string& outError);
	};
}
