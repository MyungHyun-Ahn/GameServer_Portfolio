#include "EchoServerPch.h"

#include "Generated/Config/EchoServer/EchoServerConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::EchoServer
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<EBackend>, 2> kEchoServerBackendEnumValues = {
		{{"Iocp", EBackend::Iocp}, {"Rio", EBackend::Rio}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ERioSendDispatchMode>, 2> kEchoServerRioSendDispatchModeEnumValues = {
		{{"Direct", ERioSendDispatchMode::Direct}, {"OwnerThread", ERioSendDispatchMode::OwnerThread}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELogMinimumLevel>, 4> kEchoServerLogMinimumLevelEnumValues = {
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

	constexpr std::array<Foundation::Config::SConfigEnumValue<EDebugFirstEchoRaceInjectionMode>, 4>
		kDebugFirstEchoRaceInjectionModeEnumValues = {{{"None", EDebugFirstEchoRaceInjectionMode::None},
			{"SwitchToThread", EDebugFirstEchoRaceInjectionMode::SwitchToThread},
			{"Sleep0", EDebugFirstEchoRaceInjectionMode::Sleep0},
			{"Yield", EDebugFirstEchoRaceInjectionMode::Yield}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<EDebugContentsRaceInjectionMode>, 4>
		kDebugContentsRaceInjectionModeEnumValues = {{{"None", EDebugContentsRaceInjectionMode::None},
			{"SwitchToThread", EDebugContentsRaceInjectionMode::SwitchToThread},
			{"Sleep0", EDebugContentsRaceInjectionMode::Sleep0},
			{"Yield", EDebugContentsRaceInjectionMode::Yield}}};

	bool FEchoServerConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FEchoServerConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 2> kKnownSections = {"EchoServer", "Debug"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 21> kEchoServerKnownKeys = {"Backend",
			"RioSendDispatchMode",
			"BindIp",
			"Port",
			"WorkerThreadCount",
			"MaxSessionCount",
			"RecvBufferSize",
			"SocketSendBufferBytes",
			"LogMinimumLevel",
			"LogOutputDirectory",
			"LogConsoleEnabled",
			"LogFileEnabled",
			"LogIncludeThreadId",
			"PacketKey",
			"EnablePagePool",
			"PageSize",
			"ContentsWorkerThreadCount",
			"SendThreadCount",
			"ResponsesPerThread",
			"RoomCount",
			"RoomCapacity"};

		if (!reader.ValidateKnownKeys("EchoServer", kEchoServerKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 19> kDebugKnownKeys = {"ManualDump",
			"Headless",
			"BootstrapTrace",
			"TraceUserId",
			"LogPackets",
			"TransitionRaceInjectionEnabled",
			"TransitionRaceInjectionMode",
			"PostRoomChangeRaceInjectionEnabled",
			"PostRoomChangeRaceInjectionMode",
			"FirstEchoRaceInjectionEnabled",
			"FirstEchoRaceInjectionMode",
			"ContentsRaceInjectionEnabled",
			"ContentsRaceInjectionPeriod",
			"ContentsRaceInjectionMode",
			"ContentsFailFast",
			"DelegateTestEnabled",
			"DelegateTestTargetRoomId",
			"DelegateTestSleepMs",
			"DelegateTestSleepEveryNFrames"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("EchoServer", "Backend", kEchoServerBackendEnumValues, outConfig.EchoServer.Backend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("EchoServer",
				"RioSendDispatchMode",
				kEchoServerRioSendDispatchModeEnumValues,
				outConfig.EchoServer.RioSendDispatchMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("EchoServer", "BindIp", outConfig.EchoServer.BindIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("EchoServer", "Port", outConfig.EchoServer.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "WorkerThreadCount", outConfig.EchoServer.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "MaxSessionCount", outConfig.EchoServer.MaxSessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "RecvBufferSize", outConfig.EchoServer.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "SocketSendBufferBytes", outConfig.EchoServer.SocketSendBufferBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum(
				"EchoServer", "LogMinimumLevel", kEchoServerLogMinimumLevelEnumValues, outConfig.EchoServer.LogMinimumLevel, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("EchoServer", "LogOutputDirectory", outConfig.EchoServer.LogOutputDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("EchoServer", "LogConsoleEnabled", outConfig.EchoServer.LogConsoleEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("EchoServer", "LogFileEnabled", outConfig.EchoServer.LogFileEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("EchoServer", "LogIncludeThreadId", outConfig.EchoServer.LogIncludeThreadId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("EchoServer", "PacketKey", outConfig.EchoServer.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("EchoServer", "EnablePagePool", outConfig.EchoServer.EnablePagePool, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("EchoServer", "PageSize", outConfig.EchoServer.PageSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "ContentsWorkerThreadCount", outConfig.EchoServer.ContentsWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "SendThreadCount", outConfig.EchoServer.SendThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "ResponsesPerThread", outConfig.EchoServer.ResponsesPerThread, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "RoomCount", outConfig.EchoServer.RoomCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoServer", "RoomCapacity", outConfig.EchoServer.RoomCapacity, outError))
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

		if (!reader.ReadOptionalBool("Debug", "FirstEchoRaceInjectionEnabled", outConfig.Debug.FirstEchoRaceInjectionEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("Debug",
				"FirstEchoRaceInjectionMode",
				kDebugFirstEchoRaceInjectionModeEnumValues,
				outConfig.Debug.FirstEchoRaceInjectionMode,
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

		if (!reader.ReadOptionalBool("Debug", "DelegateTestEnabled", outConfig.Debug.DelegateTestEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("Debug", "DelegateTestTargetRoomId", outConfig.Debug.DelegateTestTargetRoomId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "DelegateTestSleepMs", outConfig.Debug.DelegateTestSleepMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "DelegateTestSleepEveryNFrames", outConfig.Debug.DelegateTestSleepEveryNFrames, outError))
		{
			return false;
		}

		return true;
	}
}
