#include "WorldServerPch.h"

#include "Generated/Config/WorldServer/WorldServerConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::WorldServer
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<EBackend>, 2> kWorldServerBackendEnumValues = {
		{{"Iocp", EBackend::Iocp}, {"Rio", EBackend::Rio}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ERioSendDispatchMode>, 2> kWorldServerRioSendDispatchModeEnumValues = {
		{{"Direct", ERioSendDispatchMode::Direct}, {"OwnerThread", ERioSendDispatchMode::OwnerThread}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<EAuthMode>, 2> kWorldServerAuthModeEnumValues = {
		{{"Disabled", EAuthMode::Disabled}, {"Redis", EAuthMode::Redis}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELogMinimumLevel>, 4> kWorldServerLogMinimumLevelEnumValues = {
		{{"Debug", ELogMinimumLevel::Debug},
			{"Info", ELogMinimumLevel::Info},
			{"Warn", ELogMinimumLevel::Warn},
			{"Error", ELogMinimumLevel::Error}}};

	bool FWorldServerConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FWorldServerConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 2> kKnownSections = {"WorldServer", "Debug"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 38> kWorldServerKnownKeys = {"Backend",
			"RioSendDispatchMode",
			"BindIp",
			"Port",
			"WorkerThreadCount",
			"MaxSessionCount",
			"RecvBufferSize",
			"SocketSendBufferBytes",
			"RioSendRingSizeBytes",
			"PacketKey",
			"ContentsWorkerThreadCount",
			"SectorExecutorInstanceCount",
			"SectorTaskPumpBatchSize",
			"MapContentShardCount",
			"MapTickFps",
			"MovementCorrectionTolerance",
			"GameDataDirectory",
			"AuthMode",
			"LoginRedisHost",
			"LoginRedisPort",
			"LoginRedisPassword",
			"LoginRedisDatabase",
			"LoginRedisConnectTimeoutMilliseconds",
			"WorldTicketKeyPrefix",
			"ActiveLoginKeyPrefix",
			"CacheEnabled",
			"CacheHost",
			"CachePort",
			"CacheRpcPacketKey",
			"RpcServerInstanceId",
			"CacheServerInstanceId",
			"CacheRpcTimeoutMilliseconds",
			"CacheReconnectMilliseconds",
			"LogMinimumLevel",
			"LogOutputDirectory",
			"LogConsoleEnabled",
			"LogFileEnabled",
			"LogIncludeThreadId"};

		if (!reader.ValidateKnownKeys("WorldServer", kWorldServerKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 1> kDebugKnownKeys = {"Headless"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("WorldServer", "Backend", kWorldServerBackendEnumValues, outConfig.WorldServer.Backend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("WorldServer",
				"RioSendDispatchMode",
				kWorldServerRioSendDispatchModeEnumValues,
				outConfig.WorldServer.RioSendDispatchMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "BindIp", outConfig.WorldServer.BindIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("WorldServer", "Port", outConfig.WorldServer.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "WorkerThreadCount", outConfig.WorldServer.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "MaxSessionCount", outConfig.WorldServer.MaxSessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "RecvBufferSize", outConfig.WorldServer.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "SocketSendBufferBytes", outConfig.WorldServer.SocketSendBufferBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "RioSendRingSizeBytes", outConfig.WorldServer.RioSendRingSizeBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "PacketKey", outConfig.WorldServer.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"WorldServer", "ContentsWorkerThreadCount", outConfig.WorldServer.ContentsWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"WorldServer", "SectorExecutorInstanceCount", outConfig.WorldServer.SectorExecutorInstanceCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "SectorTaskPumpBatchSize", outConfig.WorldServer.SectorTaskPumpBatchSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "MapContentShardCount", outConfig.WorldServer.MapContentShardCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "MapTickFps", outConfig.WorldServer.MapTickFps, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalFloat(
				"WorldServer", "MovementCorrectionTolerance", outConfig.WorldServer.MovementCorrectionTolerance, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "GameDataDirectory", outConfig.WorldServer.GameDataDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("WorldServer", "AuthMode", kWorldServerAuthModeEnumValues, outConfig.WorldServer.AuthMode, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "LoginRedisHost", outConfig.WorldServer.LoginRedisHost, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("WorldServer", "LoginRedisPort", outConfig.WorldServer.LoginRedisPort, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("WorldServer", "LoginRedisPassword", outConfig.WorldServer.LoginRedisPassword, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("WorldServer", "LoginRedisDatabase", outConfig.WorldServer.LoginRedisDatabase, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer",
				"LoginRedisConnectTimeoutMilliseconds",
				outConfig.WorldServer.LoginRedisConnectTimeoutMilliseconds,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "WorldTicketKeyPrefix", outConfig.WorldServer.WorldTicketKeyPrefix, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "ActiveLoginKeyPrefix", outConfig.WorldServer.ActiveLoginKeyPrefix, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("WorldServer", "CacheEnabled", outConfig.WorldServer.CacheEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("WorldServer", "CacheHost", outConfig.WorldServer.CacheHost, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("WorldServer", "CachePort", outConfig.WorldServer.CachePort, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "CacheRpcPacketKey", outConfig.WorldServer.CacheRpcPacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "RpcServerInstanceId", outConfig.WorldServer.RpcServerInstanceId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("WorldServer", "CacheServerInstanceId", outConfig.WorldServer.CacheServerInstanceId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"WorldServer", "CacheRpcTimeoutMilliseconds", outConfig.WorldServer.CacheRpcTimeoutMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"WorldServer", "CacheReconnectMilliseconds", outConfig.WorldServer.CacheReconnectMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum(
				"WorldServer", "LogMinimumLevel", kWorldServerLogMinimumLevelEnumValues, outConfig.WorldServer.LogMinimumLevel, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("WorldServer", "LogOutputDirectory", outConfig.WorldServer.LogOutputDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("WorldServer", "LogConsoleEnabled", outConfig.WorldServer.LogConsoleEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("WorldServer", "LogFileEnabled", outConfig.WorldServer.LogFileEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("WorldServer", "LogIncludeThreadId", outConfig.WorldServer.LogIncludeThreadId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "Headless", outConfig.Debug.Headless, outError))
		{
			return false;
		}

		return true;
	}
}
