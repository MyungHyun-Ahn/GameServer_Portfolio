#include "ChattingServerPch.h"

#include "Generated/Config/ChattingServer/ChattingServerConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::ChattingServer
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<EBackend>, 2> kChattingServerBackendEnumValues = {
		{{"Iocp", EBackend::Iocp}, {"Rio", EBackend::Rio}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ERioSendDispatchMode>, 2> kChattingServerRioSendDispatchModeEnumValues = {
		{{"Direct", ERioSendDispatchMode::Direct}, {"OwnerThread", ERioSendDispatchMode::OwnerThread}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELogMinimumLevel>, 4> kChattingServerLogMinimumLevelEnumValues = {
		{{"Debug", ELogMinimumLevel::Debug},
			{"Info", ELogMinimumLevel::Info},
			{"Warn", ELogMinimumLevel::Warn},
			{"Error", ELogMinimumLevel::Error}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<EDebugTransitionRaceInjectionMode>, 4>
		kDebugTransitionRaceInjectionModeEnumValues = {{{"None", EDebugTransitionRaceInjectionMode::None},
			{"SwitchToThread", EDebugTransitionRaceInjectionMode::SwitchToThread},
			{"Sleep0", EDebugTransitionRaceInjectionMode::Sleep0},
			{"Yield", EDebugTransitionRaceInjectionMode::Yield}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<EDebugPostRoomChangeRaceInjectionMode>, 4>
		kDebugPostRoomChangeRaceInjectionModeEnumValues = {{{"None", EDebugPostRoomChangeRaceInjectionMode::None},
			{"SwitchToThread", EDebugPostRoomChangeRaceInjectionMode::SwitchToThread},
			{"Sleep0", EDebugPostRoomChangeRaceInjectionMode::Sleep0},
			{"Yield", EDebugPostRoomChangeRaceInjectionMode::Yield}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<EDebugContentsRaceInjectionMode>, 4>
		kDebugContentsRaceInjectionModeEnumValues = {{{"None", EDebugContentsRaceInjectionMode::None},
			{"SwitchToThread", EDebugContentsRaceInjectionMode::SwitchToThread},
			{"Sleep0", EDebugContentsRaceInjectionMode::Sleep0},
			{"Yield", EDebugContentsRaceInjectionMode::Yield}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELoginAuthMode>, 2> kLoginAuthModeEnumValues = {
		{{"Disabled", ELoginAuthMode::Disabled}, {"Redis", ELoginAuthMode::Redis}}};

	bool FChattingServerConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FChattingServerConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 3> kKnownSections = {"ChattingServer", "Debug", "LoginAuth"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 21> kChattingServerKnownKeys = {"Backend",
			"RioSendDispatchMode",
			"BindIp",
			"Port",
			"WorkerThreadCount",
			"MaxSessionCount",
			"RecvBufferSize",
			"SocketSendBufferBytes",
			"RioSendRingSizeBytes",
			"LogMinimumLevel",
			"LogOutputDirectory",
			"LogConsoleEnabled",
			"LogFileEnabled",
			"LogIncludeThreadId",
			"PacketKey",
			"EnablePagePool",
			"PageSize",
			"ContentsWorkerThreadCount",
			"RoomCount",
			"RoomCapacity",
			"MaxChatPayloadBytes"};

		if (!reader.ValidateKnownKeys("ChattingServer", kChattingServerKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 13> kDebugKnownKeys = {"ManualDump",
			"Headless",
			"BootstrapTrace",
			"TraceUserId",
			"LogPackets",
			"TransitionRaceInjectionEnabled",
			"TransitionRaceInjectionMode",
			"PostRoomChangeRaceInjectionEnabled",
			"PostRoomChangeRaceInjectionMode",
			"ContentsRaceInjectionEnabled",
			"ContentsRaceInjectionPeriod",
			"ContentsRaceInjectionMode",
			"ContentsFailFast"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 7> kLoginAuthKnownKeys = {
			"Mode", "RedisHost", "RedisPort", "RedisPassword", "RedisDatabase", "RedisConnectTimeoutMs", "RedisKeyPrefix"};

		if (!reader.ValidateKnownKeys("LoginAuth", kLoginAuthKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum(
				"ChattingServer", "Backend", kChattingServerBackendEnumValues, outConfig.ChattingServer.Backend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("ChattingServer",
				"RioSendDispatchMode",
				kChattingServerRioSendDispatchModeEnumValues,
				outConfig.ChattingServer.RioSendDispatchMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("ChattingServer", "BindIp", outConfig.ChattingServer.BindIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("ChattingServer", "Port", outConfig.ChattingServer.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "WorkerThreadCount", outConfig.ChattingServer.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "MaxSessionCount", outConfig.ChattingServer.MaxSessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "RecvBufferSize", outConfig.ChattingServer.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "SocketSendBufferBytes", outConfig.ChattingServer.SocketSendBufferBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("ChattingServer", "RioSendRingSizeBytes", outConfig.ChattingServer.RioSendRingSizeBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("ChattingServer",
				"LogMinimumLevel",
				kChattingServerLogMinimumLevelEnumValues,
				outConfig.ChattingServer.LogMinimumLevel,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("ChattingServer", "LogOutputDirectory", outConfig.ChattingServer.LogOutputDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("ChattingServer", "LogConsoleEnabled", outConfig.ChattingServer.LogConsoleEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("ChattingServer", "LogFileEnabled", outConfig.ChattingServer.LogFileEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("ChattingServer", "LogIncludeThreadId", outConfig.ChattingServer.LogIncludeThreadId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("ChattingServer", "PacketKey", outConfig.ChattingServer.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("ChattingServer", "EnablePagePool", outConfig.ChattingServer.EnablePagePool, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("ChattingServer", "PageSize", outConfig.ChattingServer.PageSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"ChattingServer", "ContentsWorkerThreadCount", outConfig.ChattingServer.ContentsWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "RoomCount", outConfig.ChattingServer.RoomCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "RoomCapacity", outConfig.ChattingServer.RoomCapacity, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingServer", "MaxChatPayloadBytes", outConfig.ChattingServer.MaxChatPayloadBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "ManualDump", outConfig.Debug.ManualDump, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "Headless", outConfig.Debug.Headless, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "BootstrapTrace", outConfig.Debug.BootstrapTrace, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("Debug", "TraceUserId", outConfig.Debug.TraceUserId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "LogPackets", outConfig.Debug.LogPackets, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "TransitionRaceInjectionEnabled", outConfig.Debug.TransitionRaceInjectionEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("Debug",
				"TransitionRaceInjectionMode",
				kDebugTransitionRaceInjectionModeEnumValues,
				outConfig.Debug.TransitionRaceInjectionMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool(
				"Debug", "PostRoomChangeRaceInjectionEnabled", outConfig.Debug.PostRoomChangeRaceInjectionEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("Debug",
				"PostRoomChangeRaceInjectionMode",
				kDebugPostRoomChangeRaceInjectionModeEnumValues,
				outConfig.Debug.PostRoomChangeRaceInjectionMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "ContentsRaceInjectionEnabled", outConfig.Debug.ContentsRaceInjectionEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("Debug", "ContentsRaceInjectionPeriod", outConfig.Debug.ContentsRaceInjectionPeriod, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("Debug",
				"ContentsRaceInjectionMode",
				kDebugContentsRaceInjectionModeEnumValues,
				outConfig.Debug.ContentsRaceInjectionMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "ContentsFailFast", outConfig.Debug.ContentsFailFast, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("LoginAuth", "Mode", kLoginAuthModeEnumValues, outConfig.LoginAuth.Mode, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("LoginAuth", "RedisHost", outConfig.LoginAuth.RedisHost, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt16("LoginAuth", "RedisPort", outConfig.LoginAuth.RedisPort, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("LoginAuth", "RedisPassword", outConfig.LoginAuth.RedisPassword, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("LoginAuth", "RedisDatabase", outConfig.LoginAuth.RedisDatabase, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("LoginAuth", "RedisConnectTimeoutMs", outConfig.LoginAuth.RedisConnectTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("LoginAuth", "RedisKeyPrefix", outConfig.LoginAuth.RedisKeyPrefix, outError))
		{
			return false;
		}

		return true;
	}
}
