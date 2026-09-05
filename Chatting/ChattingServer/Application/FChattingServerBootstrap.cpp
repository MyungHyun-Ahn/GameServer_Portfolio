#include "ChattingServerPch.h"

#include "ChattingServer/Application/FChattingServerBootstrap.h"

#include "Foundation/Diagnostics/FCrashDump.h"
#include "Foundation/Logging/FCompositeLogger.h"
#include "Foundation/Logging/FConsoleLogger.h"
#include "Foundation/Logging/FFileLogger.h"
#include "Foundation/Logging/ILogger.h"
#include "ContentsRuntime/Core/FContentInstanceIdAllocator.h"
#include "ContentsRuntime/Routing/FContentRuntime.h"
#include "Connector/Config/RedisChatTicketStoreTypes.h"
#include "Connector/Interfaces/IChatTicketStore.h"
#include "Connector/Redis/FDisabledChatTicketStore.h"
#include "Connector/Redis/FRedisChatTicketStore.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "ChattingServer/Contents/Auth/FAuthContent.h"
#include "ChattingServer/Contents/ContentTypes.h"
#include "ChattingServer/Contents/Lobby/FLobbyContent.h"
#include "ChattingServer/Contents/Room/FChatRoomContent.h"
#include "ChattingServer/Contents/Room/FRoomRegistry.h"
#include "ChattingServer/Contents/Session/FUserRegistry.h"
#include "Generated/Config/ChattingServer/ChattingServerConfig.h"
#include "Generated/Packets/Cpp/Chatting/ChattingPackets.h"
#include "Generated/Packets/Cpp/Login/LoginPackets.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Servers/Core/BackendTypes.h"
#include "Servers/Core/FServerFactory.h"
#include "Servers/Session/FRioSession.h"
#include "Servers/IApplicationHandler.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <Psapi.h>
#include <stdexcept>
#include <thread>
#include <Windows.h>

#include <format>
#pragma comment(lib, "Psapi.lib")

namespace
{
	enum class ELoginAuthMode
	{
		Disabled,
		Redis
	};

	struct SLoginAuthRuntimeConfig
	{
		ELoginAuthMode mode = ELoginAuthMode::Disabled;
		Connector::SRedisChatTicketStoreConfig redis;
	};

	bool ShouldTraceSession(
		const ChattingServer::Contents::SRuntimeOptions& runtimeOptions,
		const std::uint64_t sessionId) noexcept
	{
		if (!runtimeOptions.bootstrapTrace)
		{
			return false;
		}

		if (runtimeOptions.tracedSessionId == nullptr)
		{
			return true;
		}

		const std::uint64_t tracedSessionId = runtimeOptions.tracedSessionId->load(std::memory_order_relaxed);
		return tracedSessionId != 0 && tracedSessionId == sessionId;
	}

	struct SProcessMetricsSnapshot
	{
		ULONGLONG tickCountMs = 0;
		std::uint64_t processTime100ns = 0;
		SIZE_T workingSetBytes = 0;
		SIZE_T peakWorkingSetBytes = 0;
		bool valid = false;
	};

	std::uint64_t FileTimeToUInt64(
		const FILETIME& fileTime) noexcept
	{
		ULARGE_INTEGER value{};
		value.LowPart = fileTime.dwLowDateTime;
		value.HighPart = fileTime.dwHighDateTime;
		return value.QuadPart;
	}

	SProcessMetricsSnapshot CaptureProcessMetricsSnapshot() noexcept
	{
		SProcessMetricsSnapshot snapshot{};
		snapshot.tickCountMs = GetTickCount64();

		FILETIME creationTime{};
		FILETIME exitTime{};
		FILETIME kernelTime{};
		FILETIME userTime{};
		if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
		{
			return snapshot;
		}

		PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
		if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters), sizeof(memoryCounters)))
		{
			return snapshot;
		}

		snapshot.processTime100ns = FileTimeToUInt64(kernelTime) + FileTimeToUInt64(userTime);
		snapshot.workingSetBytes = memoryCounters.WorkingSetSize;
		snapshot.peakWorkingSetBytes = memoryCounters.PeakWorkingSetSize;
		snapshot.valid = true;
		return snapshot;
	}

	double CalculateCpuUsagePercent(
		const SProcessMetricsSnapshot& previous,
		const SProcessMetricsSnapshot& current) noexcept
	{
		if (!previous.valid || !current.valid || current.tickCountMs <= previous.tickCountMs)
		{
			return 0.0;
		}

		const std::uint64_t wallTime100ns = static_cast<std::uint64_t>(current.tickCountMs - previous.tickCountMs) * 10000ULL;
		if (wallTime100ns == 0 || current.processTime100ns < previous.processTime100ns)
		{
			return 0.0;
		}

		SYSTEM_INFO systemInfo{};
		GetSystemInfo(&systemInfo);
		const std::uint32_t logicalProcessorCount =
			std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(systemInfo.dwNumberOfProcessors));
		const double processTimeDelta = static_cast<double>(current.processTime100ns - previous.processTime100ns);
		const double totalTime = static_cast<double>(wallTime100ns) * static_cast<double>(logicalProcessorCount);
		if (totalTime <= 0.0)
		{
			return 0.0;
		}

		return (processTimeDelta / totalTime) * 100.0;
	}

	double BytesToMegabytes(
		const SIZE_T bytes) noexcept
	{
		return static_cast<double>(bytes) / (1024.0 * 1024.0);
	}

	ContentsRuntime::Core::SContentRuntimeContentStats AggregateContentStats(
		const ContentsRuntime::Core::SContentRuntimeStats& stats,
		const ContentsRuntime::Core::FContentId contentId) noexcept
	{
		ContentsRuntime::Core::SContentRuntimeContentStats aggregated{};
		aggregated.contentId = contentId;

		for (const auto& contentStats : stats.contents)
		{
			if (contentStats.contentId == contentId)
			{
				aggregated.activeSessionCount += contentStats.activeSessionCount;
				aggregated.threadStats.contentId = contentId;
				aggregated.threadStats.enqueueEnterCallCount += contentStats.threadStats.enqueueEnterCallCount;
				aggregated.threadStats.enqueueLeaveCallCount += contentStats.threadStats.enqueueLeaveCallCount;
				aggregated.threadStats.enqueuePacketCallCount += contentStats.threadStats.enqueuePacketCallCount;
				aggregated.threadStats.enterCount += contentStats.threadStats.enterCount;
				aggregated.threadStats.leaveCount += contentStats.threadStats.leaveCount;
				aggregated.threadStats.packetCount += contentStats.threadStats.packetCount;
				aggregated.threadStats.frameCount += contentStats.threadStats.frameCount;
				aggregated.threadStats.enterQueueDepth += contentStats.threadStats.enterQueueDepth;
				aggregated.threadStats.leaveQueueDepth += contentStats.threadStats.leaveQueueDepth;
				aggregated.threadStats.packetQueueDepth += contentStats.threadStats.packetQueueDepth;
				aggregated.threadStats.maxEnterQueueDepth =
					std::max(aggregated.threadStats.maxEnterQueueDepth, contentStats.threadStats.maxEnterQueueDepth);
				aggregated.threadStats.maxLeaveQueueDepth =
					std::max(aggregated.threadStats.maxLeaveQueueDepth, contentStats.threadStats.maxLeaveQueueDepth);
				aggregated.threadStats.maxPacketQueueDepth =
					std::max(aggregated.threadStats.maxPacketQueueDepth, contentStats.threadStats.maxPacketQueueDepth);
				aggregated.threadStats.enqueueEnterLockWaitNs += contentStats.threadStats.enqueueEnterLockWaitNs;
				aggregated.threadStats.enqueueLeaveLockWaitNs += contentStats.threadStats.enqueueLeaveLockWaitNs;
				aggregated.threadStats.enqueuePacketLockWaitNs += contentStats.threadStats.enqueuePacketLockWaitNs;
				aggregated.threadStats.maxEnqueueEnterLockWaitNs =
					std::max(aggregated.threadStats.maxEnqueueEnterLockWaitNs, contentStats.threadStats.maxEnqueueEnterLockWaitNs);
				aggregated.threadStats.maxEnqueueLeaveLockWaitNs =
					std::max(aggregated.threadStats.maxEnqueueLeaveLockWaitNs, contentStats.threadStats.maxEnqueueLeaveLockWaitNs);
				aggregated.threadStats.maxEnqueuePacketLockWaitNs =
					std::max(aggregated.threadStats.maxEnqueuePacketLockWaitNs, contentStats.threadStats.maxEnqueuePacketLockWaitNs);
				aggregated.threadStats.lastDelayFrame =
					std::max(aggregated.threadStats.lastDelayFrame, contentStats.threadStats.lastDelayFrame);
				aggregated.threadStats.maxDelayFrame =
					std::max(aggregated.threadStats.maxDelayFrame, contentStats.threadStats.maxDelayFrame);
			}
		}

		return aggregated;
	}

	std::uint64_t DeltaThreadCount(
		const ContentsRuntime::Core::SContentRuntimeContentStats& currentStats,
		const ContentsRuntime::Core::SContentRuntimeContentStats& previousStats,
		std::uint64_t ContentsRuntime::Core::SContentThreadStats::* member) noexcept
	{
		const std::uint64_t currentValue = currentStats.threadStats.*member;
		const std::uint64_t previousValue = previousStats.threadStats.*member;
		return currentValue >= previousValue ? (currentValue - previousValue) : 0;
	}

	double ToMicroseconds(
		std::uint64_t nanoseconds) noexcept
	{
		return static_cast<double>(nanoseconds) / 1000.0;
	}

	std::filesystem::path GetExecutableDirectory()
	{
		std::array<char, MAX_PATH> modulePath = {};
		const DWORD pathLength = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (pathLength == 0 || pathLength >= modulePath.size())
		{
			return std::filesystem::current_path();
		}

		return std::filesystem::path(modulePath.data()).parent_path();
	}

	std::string ToLowerAscii(
		std::string value)
	{
		for (char& character : value)
		{
			if (character >= 'A' && character <= 'Z')
			{
				character = static_cast<char>(character - 'A' + 'a');
			}
		}

		return value;
	}

	std::optional<std::filesystem::path> TryGetConfigPathOverride(
		int argc,
		char* argv[])
	{
		for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
		{
			if (std::string_view(argv[argumentIndex]) == "--config" && argumentIndex + 1 < argc)
			{
				return std::filesystem::path(argv[argumentIndex + 1]);
			}
		}

		return std::nullopt;
	}

	std::filesystem::path ResolveDefaultChattingServerConfigPath(
		const std::filesystem::path& executableDirectory)
	{
		const std::filesystem::path localPath = executableDirectory / "Config" / "Server" / "ChattingServer.yaml";
		if (std::filesystem::exists(localPath))
		{
			return localPath;
		}

		return executableDirectory.parent_path() / "Config" / "Server" / "ChattingServer.yaml";
	}

	std::filesystem::path ResolveConfiguredPath(
		const std::filesystem::path& executableDirectory,
		const std::string& configuredPath)
	{
		if (configuredPath.empty())
		{
			return {};
		}

		const std::filesystem::path path(configuredPath);
		if (path.is_absolute())
		{
			return path;
		}

		return executableDirectory.parent_path() / path;
	}

	NetworkLib::Core::EBackendKind ToBackendKind(
		const Generated::Config::ChattingServer::EBackend backend) noexcept
	{
		switch (backend)
		{
			case Generated::Config::ChattingServer::EBackend::Iocp:
				return NetworkLib::Core::EBackendKind::Iocp;
			case Generated::Config::ChattingServer::EBackend::Rio:
				return NetworkLib::Core::EBackendKind::Rio;
		}

		return NetworkLib::Core::EBackendKind::Iocp;
	}

	NetworkLib::Core::ERioSendDispatchMode ToRioSendDispatchMode(
		const Generated::Config::ChattingServer::ERioSendDispatchMode sendDispatchMode) noexcept
	{
		switch (sendDispatchMode)
		{
			case Generated::Config::ChattingServer::ERioSendDispatchMode::Direct:
				return NetworkLib::Core::ERioSendDispatchMode::Direct;
			case Generated::Config::ChattingServer::ERioSendDispatchMode::OwnerThread:
				return NetworkLib::Core::ERioSendDispatchMode::OwnerThread;
		}

		return NetworkLib::Core::ERioSendDispatchMode::Direct;
	}

	Foundation::ELogLevel ToLogLevel(
		const Generated::Config::ChattingServer::ELogMinimumLevel logLevel) noexcept
	{
		switch (logLevel)
		{
			case Generated::Config::ChattingServer::ELogMinimumLevel::Debug:
				return Foundation::ELogLevel::Debug;
			case Generated::Config::ChattingServer::ELogMinimumLevel::Info:
				return Foundation::ELogLevel::Info;
			case Generated::Config::ChattingServer::ELogMinimumLevel::Warn:
				return Foundation::ELogLevel::Warn;
			case Generated::Config::ChattingServer::ELogMinimumLevel::Error:
				return Foundation::ELogLevel::Error;
		}

		return Foundation::ELogLevel::Info;
	}

	ELoginAuthMode ToLoginAuthMode(
		const Generated::Config::ChattingServer::ELoginAuthMode mode) noexcept
	{
		switch (mode)
		{
			case Generated::Config::ChattingServer::ELoginAuthMode::Disabled:
				return ELoginAuthMode::Disabled;
			case Generated::Config::ChattingServer::ELoginAuthMode::Redis:
				return ELoginAuthMode::Redis;
		}

		return ELoginAuthMode::Disabled;
	}

	ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode ToTransitionRaceMode(
		const Generated::Config::ChattingServer::EDebugTransitionRaceInjectionMode raceMode) noexcept
	{
		switch (raceMode)
		{
			case Generated::Config::ChattingServer::EDebugTransitionRaceInjectionMode::None:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
			case Generated::Config::ChattingServer::EDebugTransitionRaceInjectionMode::SwitchToThread:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread;
			case Generated::Config::ChattingServer::EDebugTransitionRaceInjectionMode::Sleep0:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0;
			case Generated::Config::ChattingServer::EDebugTransitionRaceInjectionMode::Yield:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Yield;
		}

		return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
	}

	ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode ToTransitionRaceMode(
		const Generated::Config::ChattingServer::EDebugPostRoomChangeRaceInjectionMode raceMode) noexcept
	{
		switch (raceMode)
		{
			case Generated::Config::ChattingServer::EDebugPostRoomChangeRaceInjectionMode::None:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
			case Generated::Config::ChattingServer::EDebugPostRoomChangeRaceInjectionMode::SwitchToThread:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread;
			case Generated::Config::ChattingServer::EDebugPostRoomChangeRaceInjectionMode::Sleep0:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0;
			case Generated::Config::ChattingServer::EDebugPostRoomChangeRaceInjectionMode::Yield:
				return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Yield;
		}

		return ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
	}

	ContentsRuntime::Core::ERaceInjectionMode ToContentsRaceMode(
		const Generated::Config::ChattingServer::EDebugContentsRaceInjectionMode raceMode) noexcept
	{
		switch (raceMode)
		{
			case Generated::Config::ChattingServer::EDebugContentsRaceInjectionMode::None:
				return ContentsRuntime::Core::ERaceInjectionMode::None;
			case Generated::Config::ChattingServer::EDebugContentsRaceInjectionMode::SwitchToThread:
				return ContentsRuntime::Core::ERaceInjectionMode::SwitchToThread;
			case Generated::Config::ChattingServer::EDebugContentsRaceInjectionMode::Sleep0:
				return ContentsRuntime::Core::ERaceInjectionMode::Sleep0;
			case Generated::Config::ChattingServer::EDebugContentsRaceInjectionMode::Yield:
				return ContentsRuntime::Core::ERaceInjectionMode::Yield;
		}

		return ContentsRuntime::Core::ERaceInjectionMode::None;
	}

	bool ApplyChattingServerConfigDocument(
		const Generated::Config::ChattingServer::FChattingServerConfigDocument& configDocument,
		const std::filesystem::path& executableDirectory,
		NetworkLib::Core::SServerConfig& serverConfig,
		std::uint32_t& outPacketKey,
		bool& outRequestManualDump,
		bool& outRunHeadless,
		SLoginAuthRuntimeConfig& outLoginAuthConfig,
		ChattingServer::Contents::SRuntimeOptions& runtimeOptions,
		ContentsRuntime::Core::SContentRuntimeConfig& contentRuntimeConfig,
		std::string& outError)
	{
		if (configDocument.ChattingServer.PacketKey > 0xFF)
		{
			outError = "ChattingServer.PacketKey must be in range 0..255.";
			return false;
		}

		if (configDocument.ChattingServer.RioSendRingSizeBytes <
			static_cast<std::uint32_t>(NetworkLib::Session::FRioSession::kMaxSendPacketSizeBytes))
		{
			std::ostringstream oss;
			oss << "ChattingServer.RioSendRingSizeBytes must be >= " << NetworkLib::Session::FRioSession::kMaxSendPacketSizeBytes << ".";
			outError = oss.str();
			return false;
		}

		serverConfig.backendKind = ToBackendKind(configDocument.ChattingServer.Backend);
		serverConfig.rioSendDispatchMode = ToRioSendDispatchMode(configDocument.ChattingServer.RioSendDispatchMode);
		serverConfig.bindIp = configDocument.ChattingServer.BindIp;
		serverConfig.port = configDocument.ChattingServer.Port;
		serverConfig.workerThreadCount = std::max(1, configDocument.ChattingServer.WorkerThreadCount);
		serverConfig.maxSessionCount = std::max(1, configDocument.ChattingServer.MaxSessionCount);
		serverConfig.recvBufferSize = std::max(1, configDocument.ChattingServer.RecvBufferSize);
		serverConfig.socketSendBufferBytes = configDocument.ChattingServer.SocketSendBufferBytes;
		serverConfig.rioSendRingSizeBytes =
			std::max<std::uint32_t>(static_cast<std::uint32_t>(NetworkLib::Session::FRioSession::kMaxSendPacketSizeBytes),
				configDocument.ChattingServer.RioSendRingSizeBytes);
		serverConfig.logConfig.minimumLevel = ToLogLevel(configDocument.ChattingServer.LogMinimumLevel);
		serverConfig.logConfig.consoleEnabled = configDocument.ChattingServer.LogConsoleEnabled;
		serverConfig.logConfig.fileEnabled = configDocument.ChattingServer.LogFileEnabled;
		serverConfig.logConfig.includeThreadId = configDocument.ChattingServer.LogIncludeThreadId;
		outPacketKey = configDocument.ChattingServer.PacketKey;

		const std::filesystem::path configuredLogPath =
			ResolveConfiguredPath(executableDirectory, configDocument.ChattingServer.LogOutputDirectory);
		if (configuredLogPath.empty())
		{
			serverConfig.logConfig.outputDirectory = (executableDirectory / "logs" / "ChattingServer").string();
		}
		else
		{
			serverConfig.logConfig.outputDirectory = configuredLogPath.string();
		}

		runtimeOptions.enablePagePool = configDocument.ChattingServer.EnablePagePool;
		runtimeOptions.pageSize = static_cast<std::uint32_t>(std::max<std::uint32_t>(1u, configDocument.ChattingServer.PageSize));
		runtimeOptions.roomCount = std::max(1, configDocument.ChattingServer.RoomCount);
		runtimeOptions.roomCapacity = std::max(1, configDocument.ChattingServer.RoomCapacity);
		runtimeOptions.maxChatPayloadBytes = static_cast<std::uint32_t>(std::max(1, configDocument.ChattingServer.MaxChatPayloadBytes));
		contentRuntimeConfig.workerThreadCount = std::max(1, configDocument.ChattingServer.ContentsWorkerThreadCount);
		runtimeOptions.bootstrapTrace = configDocument.Debug.BootstrapTrace;
		runtimeOptions.traceUserId = configDocument.Debug.TraceUserId;
		runtimeOptions.logPackets = configDocument.Debug.LogPackets;
		runtimeOptions.enableTransitionResponseRaceInjection = configDocument.Debug.TransitionRaceInjectionEnabled;
		runtimeOptions.transitionRaceInjectionMode = ToTransitionRaceMode(configDocument.Debug.TransitionRaceInjectionMode);
		runtimeOptions.enablePostRoomChangeResponseRaceInjection = configDocument.Debug.PostRoomChangeRaceInjectionEnabled;
		runtimeOptions.postRoomChangeResponseRaceInjectionMode = ToTransitionRaceMode(configDocument.Debug.PostRoomChangeRaceInjectionMode);

		contentRuntimeConfig.enableRaceInjection = configDocument.Debug.ContentsRaceInjectionEnabled;
		contentRuntimeConfig.raceInjectionPeriod = std::max<std::uint32_t>(1u, configDocument.Debug.ContentsRaceInjectionPeriod);
		contentRuntimeConfig.raceInjectionMode = ToContentsRaceMode(configDocument.Debug.ContentsRaceInjectionMode);
		contentRuntimeConfig.failFastOnRuntimeError = configDocument.Debug.ContentsFailFast;
		contentRuntimeConfig.enableOwnershipTransferPolicy = true;
		contentRuntimeConfig.ownershipTransferAllowedContentIds = {ChattingServer::Contents::kRoomContentId};

		outRequestManualDump = configDocument.Debug.ManualDump;
		outRunHeadless = configDocument.Debug.Headless;
		outLoginAuthConfig.mode = ToLoginAuthMode(configDocument.LoginAuth.Mode);
		runtimeOptions.allowLegacyLogin = outLoginAuthConfig.mode == ELoginAuthMode::Disabled;
		outLoginAuthConfig.redis.connection.host = configDocument.LoginAuth.RedisHost;
		outLoginAuthConfig.redis.connection.port = configDocument.LoginAuth.RedisPort;
		outLoginAuthConfig.redis.connection.password = configDocument.LoginAuth.RedisPassword;
		outLoginAuthConfig.redis.connection.database = configDocument.LoginAuth.RedisDatabase;
		outLoginAuthConfig.redis.connection.connectTimeoutMs = configDocument.LoginAuth.RedisConnectTimeoutMs;
		outLoginAuthConfig.redis.keyPrefix = configDocument.LoginAuth.RedisKeyPrefix;
		return true;
	}

	class FChattingServerApplication final : public NetworkLib::IApplicationHandler
	{
	public:
		FChattingServerApplication(
			std::shared_ptr<Foundation::ILogger> logger,
			SLoginAuthRuntimeConfig loginAuthConfig,
			ChattingServer::Contents::SRuntimeOptions runtimeOptions,
			ContentsRuntime::Core::SContentRuntimeConfig contentRuntimeConfig)
			: m_logger(std::move(logger))
			, m_loginAuthConfig(std::move(loginAuthConfig))
			, m_runtimeOptions(runtimeOptions)
		{
			m_contentRuntime.SetConfig(contentRuntimeConfig);
			m_roomRegistry = std::make_shared<ChattingServer::Contents::FRoomRegistry>();
			m_userRegistry = std::make_shared<ChattingServer::Contents::FUserRegistry>();
			m_chatTicketStore = CreateChatTicketStore();
			ContentsRuntime::Core::FContentInstanceIdAllocator contentInstanceIdAllocator;
			const ContentsRuntime::Core::FContentInstanceId authContentInstanceId =
				contentInstanceIdAllocator.Allocate(ChattingServer::Contents::kAuthContentId);
			const ContentsRuntime::Core::FContentInstanceId lobbyContentInstanceId =
				contentInstanceIdAllocator.Allocate(ChattingServer::Contents::kLobbyContentId);
			if (!ContentsRuntime::Core::IsValidContentInstanceId(authContentInstanceId) ||
				!ContentsRuntime::Core::IsValidContentInstanceId(lobbyContentInstanceId))
			{
				throw std::runtime_error("content instance id allocation failed.");
			}

			std::vector<ChattingServer::Contents::SRoomInfoSnapshot> roomDefinitions;
			roomDefinitions.reserve(static_cast<std::size_t>(std::max(1, m_runtimeOptions.roomCount)));
			for (int roomIndex = 0; roomIndex < std::max(1, m_runtimeOptions.roomCount); ++roomIndex)
			{
				const std::uint32_t roomOrdinal = static_cast<std::uint32_t>(roomIndex);
				const ContentsRuntime::Core::FContentInstanceId roomContentInstanceId =
					contentInstanceIdAllocator.Allocate(ChattingServer::Contents::kRoomContentId);
				if (!ContentsRuntime::Core::IsValidContentInstanceId(roomContentInstanceId))
				{
					throw std::runtime_error("room content instance id allocation failed.");
				}

				roomDefinitions.push_back({ChattingServer::Contents::MakeRoomId(roomOrdinal),
					roomContentInstanceId,
					"Room-" + std::to_string(roomOrdinal + 1),
					0,
					static_cast<std::uint32_t>(std::max(1, m_runtimeOptions.roomCapacity)),
					true});
			}
			m_roomRegistry->Initialize(roomDefinitions);

			m_contentRuntime.RegisterContent(std::make_unique<ChattingServer::Contents::FAuthContent>(
				m_logger, authContentInstanceId, m_userRegistry, m_chatTicketStore, m_runtimeOptions));
			m_contentRuntime.RegisterContent(std::make_unique<ChattingServer::Contents::FLobbyContent>(
				m_logger, lobbyContentInstanceId, m_roomRegistry, m_runtimeOptions));
			for (const auto& roomDefinition : roomDefinitions)
			{
				m_contentRuntime.RegisterContent(std::make_unique<ChattingServer::Contents::FChatRoomContent>(
					m_logger, roomDefinition.contentInstanceId, m_roomRegistry, m_userRegistry, roomDefinition.roomId, m_runtimeOptions));
			}
		}

	public:
		void OnServerStarted(
			NetworkLib::IServer& server) override
		{
			m_contentRuntime.Start(server);

			Log(Foundation::ELogLevel::Info,

				"ChattingServer started. backend={}",
				static_cast<int>(server.GetBackendKind()));
		}

		void OnClientConnected(
			std::uint64_t sessionId) override
		{
			m_contentRuntime.EnterSession(sessionId, ChattingServer::Contents::kAuthContentId);

			Log(Foundation::ELogLevel::Info, "client connected. sessionId={}", sessionId);
		}

		void OnPacketReceived(
			NetworkLib::IServer& server,
			std::uint64_t sessionId,
			const NetworkLib::Packet::View::FPacketView& packetView) override
		{
			(void)server;

			if (ShouldTraceSession(m_runtimeOptions, sessionId) &&
				(packetView.opcode == Generated::Login::FLoginRq::kOpcode || packetView.opcode == Generated::Login::FLoginAuthRq::kOpcode ||
					packetView.opcode == Generated::Chatting::FRoomListRq::kOpcode ||
					packetView.opcode == Generated::Chatting::FRoomChangeRq::kOpcode ||
					packetView.opcode == Generated::Chatting::FChattingRq::kOpcode))
			{
				Log(Foundation::ELogLevel::Info,
					"bootstrap trace: ingress packet. sessionId={} opcode={} payloadBytes={}",
					sessionId,
					packetView.opcode,
					packetView.payloadLength);
			}

			if (!m_contentRuntime.EnqueuePacket(sessionId, packetView.opcode, packetView.payload, packetView.payloadLength) &&
				m_runtimeOptions.logPackets)
			{
				Log(Foundation::ELogLevel::Warn, "Unhandled packet. sessionId={} opcode={}", sessionId, packetView.opcode);
			}
		}

		void OnClientDisconnected(
			std::uint64_t sessionId) override
		{
			if (m_userRegistry != nullptr)
			{
				m_userRegistry->RemoveUser(sessionId);
			}
			m_contentRuntime.LeaveSession(sessionId);

			Log(Foundation::ELogLevel::Info, "client disconnected. sessionId={}", sessionId);
		}

		void OnServerStopped() override
		{
			m_contentRuntime.Stop();
			Log(Foundation::ELogLevel::Info, "ChattingServer stopped.");
		}

		ContentsRuntime::Core::SContentRuntimeStats GetContentStatsSnapshot()
		{
			return m_contentRuntime.GetStatsSnapshot();
		}

	private:
		std::shared_ptr<Connector::IChatTicketStore> CreateChatTicketStore()
		{
			switch (m_loginAuthConfig.mode)
			{
				case ELoginAuthMode::Redis:
					Log(Foundation::ELogLevel::Info, "login auth mode=Redis.");
					return std::make_shared<Connector::FRedisChatTicketStore>(m_loginAuthConfig.redis);
				case ELoginAuthMode::Disabled:
				default:
					Log(Foundation::ELogLevel::Info, "login auth mode=Disabled.");
					return std::make_shared<Connector::FDisabledChatTicketStore>();
			}
		}

		void Log(
			Foundation::ELogLevel logLevel,
			const std::string& message) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(logLevel, "ChattingServer", message);
			}
		}

		template <typename... TArgs>
			requires(sizeof...(TArgs) > 0)
		void Log(
			Foundation::ELogLevel logLevel,
			std::format_string<TArgs...> format,
			TArgs&&... args) const
		{
			if (m_logger != nullptr)
			{
				m_logger->Log(logLevel, "ChattingServer", format, std::forward<TArgs>(args)...);
			}
		}

	private:
		std::shared_ptr<Foundation::ILogger> m_logger;
		std::shared_ptr<ChattingServer::Contents::FRoomRegistry> m_roomRegistry;
		std::shared_ptr<ChattingServer::Contents::FUserRegistry> m_userRegistry;
		std::shared_ptr<Connector::IChatTicketStore> m_chatTicketStore;
		SLoginAuthRuntimeConfig m_loginAuthConfig;
		ChattingServer::Contents::SRuntimeOptions m_runtimeOptions;
		ContentsRuntime::Routing::FContentRuntime m_contentRuntime;
	};
}

int ChattingServer::Application::RunChattingServer(
	int argc,
	char* argv[])
{
	NetworkLib::Core::SServerConfig serverConfig{};
	std::uint32_t packetKey = 0x37;
	bool requestManualDump = false;
	bool runHeadless = false;
	SLoginAuthRuntimeConfig loginAuthConfig{};
	ChattingServer::Contents::SRuntimeOptions runtimeOptions{};
	ContentsRuntime::Core::SContentRuntimeConfig contentRuntimeConfig{};
	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	Generated::Config::ChattingServer::FChattingServerConfigDocument configDocument{};
	std::string configErrorMessage;
	const std::filesystem::path configPath =
		TryGetConfigPathOverride(argc, argv).value_or(ResolveDefaultChattingServerConfigPath(executableDirectory));
	if (!Generated::Config::ChattingServer::FChattingServerConfigLoader::LoadFromFile(configPath, configDocument, configErrorMessage))
	{
		std::cerr << "ChattingServer config load failed: " << configErrorMessage << "\n";
		return 1;
	}

	if (!ApplyChattingServerConfigDocument(configDocument,
			executableDirectory,
			serverConfig,
			packetKey,
			requestManualDump,
			runHeadless,
			loginAuthConfig,
			runtimeOptions,
			contentRuntimeConfig,
			configErrorMessage))
	{
		std::cerr << "ChattingServer config apply failed: " << configErrorMessage << "\n";
		return 1;
	}

	if (argc >= 2)
	{
		for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
		{
			const std::string argument = argv[argumentIndex];
			if (argument == "rio")
			{
				serverConfig.backendKind = NetworkLib::Core::EBackendKind::Rio;
			}
			else if (argument == "--manual-dump")
			{
				requestManualDump = true;
			}
			else if (argument == "--config" && argumentIndex + 1 < argc)
			{
				++argumentIndex;
			}
			else if (argument == "--rio-send-dispatch-mode" && argumentIndex + 1 < argc)
			{
				const std::string mode = ToLowerAscii(argv[++argumentIndex]);
				if (mode == "ownerthread" || mode == "owner")
				{
					serverConfig.rioSendDispatchMode = NetworkLib::Core::ERioSendDispatchMode::OwnerThread;
				}
				else
				{
					serverConfig.rioSendDispatchMode = NetworkLib::Core::ERioSendDispatchMode::Direct;
				}
			}
			else if (argument == "--rio-send-ring-size-bytes" && argumentIndex + 1 < argc)
			{
				serverConfig.rioSendRingSizeBytes = static_cast<std::uint32_t>(std::max(
					static_cast<int>(NetworkLib::Session::FRioSession::kMaxSendPacketSizeBytes), std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--headless")
			{
				runHeadless = true;
			}
			else if (argument == "--room-count" && argumentIndex + 1 < argc)
			{
				runtimeOptions.roomCount = std::max(1, std::atoi(argv[++argumentIndex]));
			}
			else if (argument == "--room-capacity" && argumentIndex + 1 < argc)
			{
				runtimeOptions.roomCapacity = std::max(1, std::atoi(argv[++argumentIndex]));
			}
			else if (argument == "--max-chat-payload-bytes" && argumentIndex + 1 < argc)
			{
				runtimeOptions.maxChatPayloadBytes = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--trace-user-id" && argumentIndex + 1 < argc)
			{
				runtimeOptions.traceUserId = static_cast<std::uint32_t>(std::max(0, std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--log-packets")
			{
				runtimeOptions.logPackets = true;
			}
			else if (argument == "--bootstrap-trace")
			{
				runtimeOptions.bootstrapTrace = true;
			}
			else if (argument == "--transition-race-injection")
			{
				runtimeOptions.enableTransitionResponseRaceInjection = true;
			}
			else if (argument == "--transition-race-mode" && argumentIndex + 1 < argc)
			{
				const std::string mode = argv[++argumentIndex];
				if (mode == "switch")
				{
					runtimeOptions.transitionRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread;
				}
				else if (mode == "sleep0")
				{
					runtimeOptions.transitionRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0;
				}
				else if (mode == "yield")
				{
					runtimeOptions.transitionRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Yield;
				}
				else
				{
					runtimeOptions.transitionRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
				}
			}
			else if (argument == "--post-room-change-race-injection")
			{
				runtimeOptions.enablePostRoomChangeResponseRaceInjection = true;
			}
			else if (argument == "--post-room-change-race-mode" && argumentIndex + 1 < argc)
			{
				const std::string mode = argv[++argumentIndex];
				if (mode == "switch")
				{
					runtimeOptions.postRoomChangeResponseRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::SwitchToThread;
				}
				else if (mode == "sleep0")
				{
					runtimeOptions.postRoomChangeResponseRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Sleep0;
				}
				else if (mode == "yield")
				{
					runtimeOptions.postRoomChangeResponseRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::Yield;
				}
				else
				{
					runtimeOptions.postRoomChangeResponseRaceInjectionMode =
						ChattingServer::Contents::SRuntimeOptions::ETransitionRaceInjectionMode::None;
				}
			}
			else if (argument == "--disable-page-pool")
			{
				runtimeOptions.enablePagePool = false;
			}
			else if (argument == "--page-size" && argumentIndex + 1 < argc)
			{
				runtimeOptions.pageSize = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--contents-worker-thread-count" && argumentIndex + 1 < argc)
			{
				contentRuntimeConfig.workerThreadCount = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--contents-race-injection")
			{
				contentRuntimeConfig.enableRaceInjection = true;
			}
			else if (argument == "--contents-race-period" && argumentIndex + 1 < argc)
			{
				contentRuntimeConfig.raceInjectionPeriod = static_cast<std::uint32_t>(std::max(1, std::atoi(argv[++argumentIndex])));
			}
			else if (argument == "--contents-race-mode" && argumentIndex + 1 < argc)
			{
				const std::string mode = argv[++argumentIndex];
				if (mode == "switch")
				{
					contentRuntimeConfig.raceInjectionMode = ContentsRuntime::Core::ERaceInjectionMode::SwitchToThread;
				}
				else if (mode == "sleep0")
				{
					contentRuntimeConfig.raceInjectionMode = ContentsRuntime::Core::ERaceInjectionMode::Sleep0;
				}
				else if (mode == "yield")
				{
					contentRuntimeConfig.raceInjectionMode = ContentsRuntime::Core::ERaceInjectionMode::Yield;
				}
				else
				{
					contentRuntimeConfig.raceInjectionMode = ContentsRuntime::Core::ERaceInjectionMode::None;
				}
			}
			else if (argument == "--contents-fail-fast")
			{
				contentRuntimeConfig.failFastOnRuntimeError = true;
			}
		}
	}

	if (contentRuntimeConfig.enableRaceInjection && contentRuntimeConfig.raceInjectionPeriod == 0)
	{
		contentRuntimeConfig.raceInjectionPeriod = 100;
	}

	NetworkLib::Crypto::SDefaultPacketCipherConfig packetCipherConfig{};
	packetCipherConfig.packetKey = static_cast<std::uint8_t>(packetKey);
	serverConfig.packetCipher = std::make_shared<NetworkLib::Crypto::FDefaultPacketCipher>(packetCipherConfig);
	serverConfig.packetFramer = std::make_shared<NetworkLib::Packet::Framing::FDefaultPacketFramer>();

	if (runtimeOptions.bootstrapTrace && runtimeOptions.traceUserId != 0)
	{
		runtimeOptions.tracedSessionId = std::make_shared<std::atomic<std::uint64_t>>(0);
		contentRuntimeConfig.tracedSessionId = runtimeOptions.tracedSessionId;
	}

	serverConfig.enablePageBufferReuse = runtimeOptions.enablePagePool;
	serverConfig.pageBufferSize = runtimeOptions.pageSize;

	auto compositeLogger = std::make_shared<Foundation::FCompositeLogger>();
	compositeLogger->AddSink(std::make_shared<Foundation::FConsoleLogger>(serverConfig.logConfig));
	compositeLogger->AddSink(std::make_shared<Foundation::FFileLogger>(serverConfig.logConfig));
	serverConfig.logger = compositeLogger;

	if (runtimeOptions.bootstrapTrace)
	{
		contentRuntimeConfig.enableTraceLogging = true;
		contentRuntimeConfig.traceLogger = [logger = compositeLogger](const std::string& message)
		{
			if (logger != nullptr)
			{
				logger->Log(Foundation::ELogLevel::Info, "ContentsRuntime", message);
			}
		};
	}

	Foundation::SCrashDumpConfig crashDumpConfig{};
	crashDumpConfig.outputDirectory = (executableDirectory / "dumps" / "ChattingServer").string();
	crashDumpConfig.logger = compositeLogger;
	Foundation::FCrashDump::Initialize(crashDumpConfig);

	if (requestManualDump)
	{
		const bool dumpWritten = Foundation::FCrashDump::WriteManualDumpForDiagnostics();
		Foundation::FCrashDump::Shutdown();
		return dumpWritten ? 0 : 1;
	}

	FChattingServerApplication chattingApplication(compositeLogger, loginAuthConfig, runtimeOptions, contentRuntimeConfig);
	std::unique_ptr<NetworkLib::IServer> server = NetworkLib::Core::FServerFactory::Create(serverConfig.backendKind);
	if (server == nullptr)
	{
		compositeLogger->Log(Foundation::ELogLevel::Error, "ChattingServer", "server factory failed.");
		Foundation::FCrashDump::Shutdown();
		return 1;
	}

	if (!server->Start(serverConfig, chattingApplication))
	{
		compositeLogger->Log(Foundation::ELogLevel::Error, "ChattingServer", "server start failed.");
		Foundation::FCrashDump::Shutdown();
		return 1;
	}

	if (runHeadless)
	{
		compositeLogger->Log(Foundation::ELogLevel::Info, "ChattingServer", "Headless mode enabled.");
		NetworkLib::Core::SServerStats previousStats = server->GetStatsSnapshot();
		ContentsRuntime::Core::SContentRuntimeStats previousContentStats = chattingApplication.GetContentStatsSnapshot();
		SProcessMetricsSnapshot previousProcessMetrics = CaptureProcessMetricsSnapshot();
		Foundation::FConsoleLogger::WriteLine(std::format("[BenchmarkReady] port={}", serverConfig.port));
		compositeLogger->Flush();
		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			const NetworkLib::Core::SServerStats currentStats = server->GetStatsSnapshot();
			const ContentsRuntime::Core::SContentRuntimeStats currentContentStats = chattingApplication.GetContentStatsSnapshot();
			const SProcessMetricsSnapshot currentProcessMetrics = CaptureProcessMetricsSnapshot();
			const std::uint64_t acceptTps = currentStats.acceptedSessionCount - previousStats.acceptedSessionCount;
			const std::uint64_t recvTps = currentStats.receivedPacketCount - previousStats.receivedPacketCount;
			const std::uint64_t sendTps = currentStats.sentPacketCount - previousStats.sentPacketCount;
			const std::uint64_t recvBytesPerSec = currentStats.receivedByteCount - previousStats.receivedByteCount;
			const std::uint64_t sendBytesPerSec = currentStats.sentByteCount - previousStats.sentByteCount;
			const std::uint64_t wsaRecvTps = currentStats.wsaRecvCallCount - previousStats.wsaRecvCallCount;
			const std::uint64_t wsaSendTps = currentStats.wsaSendCallCount - previousStats.wsaSendCallCount;
			const double rioPrepareAvgNs =
				currentStats.rioSendPrepareCount == 0
					? 0.0
					: static_cast<double>(currentStats.rioSendPrepareTotalNs) / static_cast<double>(currentStats.rioSendPrepareCount);
			const double rioSendRingCrossThreadRatePercent =
				currentStats.rioSendRingTouchCount == 0 ? 0.0
														: (static_cast<double>(currentStats.rioSendRingCrossThreadTouchCount) * 100.0) /
															  static_cast<double>(currentStats.rioSendRingTouchCount);
			const double rioDirectLockWaitAvgNs = currentStats.rioDirectSendRingLockCount == 0
													  ? 0.0
													  : static_cast<double>(currentStats.rioDirectSendRingLockWaitTotalNs) /
															static_cast<double>(currentStats.rioDirectSendRingLockCount);
			const double rioDirectLockHoldAvgNs = currentStats.rioDirectSendRingLockCount == 0
													  ? 0.0
													  : static_cast<double>(currentStats.rioDirectSendRingLockHoldTotalNs) /
															static_cast<double>(currentStats.rioDirectSendRingLockCount);
			const double cpuUsagePercent = CalculateCpuUsagePercent(previousProcessMetrics, currentProcessMetrics);
			const double workingSetMb = currentProcessMetrics.valid ? BytesToMegabytes(currentProcessMetrics.workingSetBytes) : 0.0;
			const double peakWorkingSetMb = currentProcessMetrics.valid ? BytesToMegabytes(currentProcessMetrics.peakWorkingSetBytes) : 0.0;
			const std::uint64_t moveTps = currentContentStats.moveSessionCount >= previousContentStats.moveSessionCount
											  ? currentContentStats.moveSessionCount - previousContentStats.moveSessionCount
											  : 0;
			const std::uint64_t enqueueFailTps = currentContentStats.enqueueFailureCount >= previousContentStats.enqueueFailureCount
													 ? currentContentStats.enqueueFailureCount - previousContentStats.enqueueFailureCount
													 : 0;
			const auto currentAuthStats = AggregateContentStats(currentContentStats, ChattingServer::Contents::kAuthContentId);
			const auto previousAuthStats = AggregateContentStats(previousContentStats, ChattingServer::Contents::kAuthContentId);
			const auto currentLobbyStats = AggregateContentStats(currentContentStats, ChattingServer::Contents::kLobbyContentId);
			const auto previousLobbyStats = AggregateContentStats(previousContentStats, ChattingServer::Contents::kLobbyContentId);
			const auto currentRoomStats = AggregateContentStats(currentContentStats, ChattingServer::Contents::kRoomContentId);
			const auto previousRoomStats = AggregateContentStats(previousContentStats, ChattingServer::Contents::kRoomContentId);
			std::ostringstream chattingStatsLine;
			chattingStatsLine << "[ChattingStats] sessions=" << currentStats.activeSessionCount << " acceptTPS=" << acceptTps
							  << " recvTPS=" << recvTps << " sendTPS=" << sendTps << " recvBps=" << recvBytesPerSec
							  << " sendBps=" << sendBytesPerSec << " wsaSendTPS=" << wsaSendTps << " wsaRecvTPS=" << wsaRecvTps
							  << " queuedSendBuffers=" << currentStats.queuedSendBufferCount
							  << " maxQueuedSendBuffers=" << currentStats.maxObservedQueuedSendBufferCount
							  << " ownerQueuedSendBytes=" << currentStats.ownerQueuedSendBytes
							  << " maxOwnerQueuedSendBytes=" << currentStats.maxObservedOwnerQueuedSendBytes
							  << " totalSendRingUsedBytes=" << currentStats.totalSendRingUsedBytes
							  << " totalSendRingInFlightBytes=" << currentStats.totalSendRingInFlightBytes
							  << " maxSessionSendRingUsedBytes=" << currentStats.maxCurrentSendRingUsedBytes
							  << " maxObservedSessionSendRingUsedBytes=" << currentStats.maxObservedSendRingUsedBytes
							  << " rioSendPrepareCount=" << currentStats.rioSendPrepareCount << " rioSendPrepareAvgNs=" << std::fixed
							  << std::setprecision(2) << rioPrepareAvgNs << " rioSendPrepareMaxNs=" << currentStats.rioSendPrepareMaxNs
							  << " rioSendRingTouchCount=" << currentStats.rioSendRingTouchCount
							  << " rioSendRingCrossThreadTouchCount=" << currentStats.rioSendRingCrossThreadTouchCount
							  << " rioSendRingCrossThreadRatePercent=" << std::fixed << std::setprecision(2)
							  << rioSendRingCrossThreadRatePercent
							  << " rioDirectSendRingLockCount=" << currentStats.rioDirectSendRingLockCount
							  << " rioDirectSendRingLockWaitAvgNs=" << std::fixed << std::setprecision(2) << rioDirectLockWaitAvgNs
							  << " rioDirectSendRingLockWaitMaxNs=" << currentStats.rioDirectSendRingLockWaitMaxNs
							  << " rioDirectSendRingLockHoldAvgNs=" << std::fixed << std::setprecision(2) << rioDirectLockHoldAvgNs
							  << " rioDirectSendRingLockHoldMaxNs=" << currentStats.rioDirectSendRingLockHoldMaxNs
							  << " sessionPool=" << currentStats.sessionPoolUsage << "/" << currentStats.sessionPoolCapacity
							  << " sendBufferPool=" << currentStats.sendBufferPoolUsage << "/" << currentStats.sendBufferPoolCapacity
							  << " packetBufferPool=" << currentStats.packetBufferPoolUsage << "/" << currentStats.packetBufferPoolCapacity
							  << " cpuPercent=" << std::fixed << std::setprecision(2) << cpuUsagePercent << " workingSetMB=" << std::fixed
							  << std::setprecision(2) << workingSetMb << " peakWorkingSetMB=" << std::fixed << std::setprecision(2)
							  << peakWorkingSetMb << " totalWSASendCalls=" << currentStats.wsaSendCallCount
							  << " totalWSARecvCalls=" << currentStats.wsaRecvCallCount;
			Foundation::FConsoleLogger::WriteLine(chattingStatsLine.str());

			std::ostringstream contentStatsLine;
			contentStatsLine
				<< "[ContentStats] contents=" << currentContentStats.registeredContentCount
				<< " sessions=" << currentContentStats.activeSessionCount << " enterCalls=" << currentContentStats.enterSessionCallCount
				<< " leaveCalls=" << currentContentStats.leaveSessionCallCount
				<< " enqueueCalls=" << currentContentStats.enqueuePacketCallCount << " moveTPS=" << moveTps
				<< " enqueueFailTPS=" << enqueueFailTps << " roomCount=" << runtimeOptions.roomCount
				<< " roomCapacity=" << runtimeOptions.roomCapacity << " runtimeEnqueueLockUs=" << std::fixed << std::setprecision(2)
				<< ToMicroseconds(currentContentStats.enqueuePacketLockWaitNs) << " runtimeEnqueueMaxLockUs=" << std::fixed
				<< std::setprecision(2) << ToMicroseconds(currentContentStats.maxEnqueuePacketLockWaitNs)
				<< " runtimeMoveLockUs=" << std::fixed << std::setprecision(2) << ToMicroseconds(currentContentStats.moveSessionLockWaitNs)
				<< " runtimeMoveMaxLockUs=" << std::fixed << std::setprecision(2)
				<< ToMicroseconds(currentContentStats.maxMoveSessionLockWaitNs) << " authSessions=" << currentAuthStats.activeSessionCount
				<< " authEnterTPS="
				<< DeltaThreadCount(currentAuthStats, previousAuthStats, &ContentsRuntime::Core::SContentThreadStats::enterCount)
				<< " authLeaveTPS="
				<< DeltaThreadCount(currentAuthStats, previousAuthStats, &ContentsRuntime::Core::SContentThreadStats::leaveCount)
				<< " authPacketTPS="
				<< DeltaThreadCount(currentAuthStats, previousAuthStats, &ContentsRuntime::Core::SContentThreadStats::packetCount)
				<< " authQueue=" << currentAuthStats.threadStats.packetQueueDepth
				<< " authMaxQueue=" << currentAuthStats.threadStats.maxPacketQueueDepth
				<< " authPacketEnqueueCalls=" << currentAuthStats.threadStats.enqueuePacketCallCount
				<< " authPacketEnqueueLockUs=" << std::fixed << std::setprecision(2)
				<< ToMicroseconds(currentAuthStats.threadStats.enqueuePacketLockWaitNs) << " authPacketEnqueueMaxLockUs=" << std::fixed
				<< std::setprecision(2) << ToMicroseconds(currentAuthStats.threadStats.maxEnqueuePacketLockWaitNs)
				<< " lobbySessions=" << currentLobbyStats.activeSessionCount << " lobbyPacketTPS="
				<< DeltaThreadCount(currentLobbyStats, previousLobbyStats, &ContentsRuntime::Core::SContentThreadStats::packetCount)
				<< " roomSessions=" << currentRoomStats.activeSessionCount << " roomEnterTPS="
				<< DeltaThreadCount(currentRoomStats, previousRoomStats, &ContentsRuntime::Core::SContentThreadStats::enterCount)
				<< " roomLeaveTPS="
				<< DeltaThreadCount(currentRoomStats, previousRoomStats, &ContentsRuntime::Core::SContentThreadStats::leaveCount)
				<< " roomPacketTPS="
				<< DeltaThreadCount(currentRoomStats, previousRoomStats, &ContentsRuntime::Core::SContentThreadStats::packetCount)
				<< " roomFrameTPS="
				<< DeltaThreadCount(currentRoomStats, previousRoomStats, &ContentsRuntime::Core::SContentThreadStats::frameCount)
				<< " roomQueue=" << currentRoomStats.threadStats.packetQueueDepth
				<< " roomMaxQueue=" << currentRoomStats.threadStats.maxPacketQueueDepth
				<< " roomPacketEnqueueCalls=" << currentRoomStats.threadStats.enqueuePacketCallCount
				<< " roomPacketEnqueueLockUs=" << std::fixed << std::setprecision(2)
				<< ToMicroseconds(currentRoomStats.threadStats.enqueuePacketLockWaitNs) << " roomPacketEnqueueMaxLockUs=" << std::fixed
				<< std::setprecision(2) << ToMicroseconds(currentRoomStats.threadStats.maxEnqueuePacketLockWaitNs)
				<< " roomLastDelayFrame=" << currentRoomStats.threadStats.lastDelayFrame
				<< " roomMaxDelayFrame=" << currentRoomStats.threadStats.maxDelayFrame;
			Foundation::FConsoleLogger::WriteLine(contentStatsLine.str());
			previousStats = currentStats;
			previousContentStats = currentContentStats;
			previousProcessMetrics = currentProcessMetrics;
		}
	}

	compositeLogger->Log(Foundation::ELogLevel::Info, "ChattingServer", "Press Enter to stop server.");
	std::cin.get();
	server->Stop();
	Foundation::FCrashDump::Shutdown();
	compositeLogger->Flush();
	return 0;
}
