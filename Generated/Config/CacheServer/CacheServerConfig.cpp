#include "CacheServerPch.h"

#include "Generated/Config/CacheServer/CacheServerConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::CacheServer
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<EBackend>, 2> kCacheServerBackendEnumValues = {
		{{"Iocp", EBackend::Iocp}, {"Rio", EBackend::Rio}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ERioSendDispatchMode>, 2> kCacheServerRioSendDispatchModeEnumValues = {
		{{"Direct", ERioSendDispatchMode::Direct}, {"OwnerThread", ERioSendDispatchMode::OwnerThread}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELogMinimumLevel>, 4> kCacheServerLogMinimumLevelEnumValues = {
		{{"Debug", ELogMinimumLevel::Debug},
			{"Info", ELogMinimumLevel::Info},
			{"Warn", ELogMinimumLevel::Warn},
			{"Error", ELogMinimumLevel::Error}}};

	bool FCacheServerConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FCacheServerConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 8> kKnownSections = {
			"CacheServer", "GameDatabase", "GamePrimary", "GameReplica1", "GameReplica2", "CachePolicy", "FaultInjection", "Debug"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 22> kCacheServerKnownKeys = {"Backend",
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
			"PlayerCacheShardCount",
			"MailboxCapacity",
			"RpcServerInstanceId",
			"GameDataDirectory",
			"DatabaseEnabled",
			"ReplicaReconnectCooldownMilliseconds",
			"LogMinimumLevel",
			"LogOutputDirectory",
			"LogConsoleEnabled",
			"LogFileEnabled",
			"LogIncludeThreadId"};

		if (!reader.ValidateKnownKeys("CacheServer", kCacheServerKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 1> kGameDatabaseKnownKeys = {"Password"};

		if (!reader.ValidateKnownKeys("GameDatabase", kGameDatabaseKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 5> kGamePrimaryKnownKeys = {"Host", "Port", "User", "Database", "ConnectTimeoutSeconds"};

		if (!reader.ValidateKnownKeys("GamePrimary", kGamePrimaryKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 6> kGameReplica1KnownKeys = {
			"Enabled", "Host", "Port", "User", "Database", "ConnectTimeoutSeconds"};

		if (!reader.ValidateKnownKeys("GameReplica1", kGameReplica1KnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 6> kGameReplica2KnownKeys = {
			"Enabled", "Host", "Port", "User", "Database", "ConnectTimeoutSeconds"};

		if (!reader.ValidateKnownKeys("GameReplica2", kGameReplica2KnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 4> kCachePolicyKnownKeys = {
			"GameOwnerLeaseMilliseconds", "IdleEvictionMilliseconds", "MaintenanceIntervalMilliseconds", "RevokeTimeoutMilliseconds"};

		if (!reader.ValidateKnownKeys("CachePolicy", kCachePolicyKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 4> kFaultInjectionKnownKeys = {"CreditBeforeDatabaseTransaction",
			"CreditAfterCommitDisconnect",
			"CreditBeforeDatabaseDelayMilliseconds",
			"CreditAfterCommitDelayMilliseconds"};

		if (!reader.ValidateKnownKeys("FaultInjection", kFaultInjectionKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 2> kDebugKnownKeys = {"RunSeconds", "Headless"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("CacheServer", "Backend", kCacheServerBackendEnumValues, outConfig.CacheServer.Backend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("CacheServer",
				"RioSendDispatchMode",
				kCacheServerRioSendDispatchModeEnumValues,
				outConfig.CacheServer.RioSendDispatchMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("CacheServer", "BindIp", outConfig.CacheServer.BindIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("CacheServer", "Port", outConfig.CacheServer.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("CacheServer", "WorkerThreadCount", outConfig.CacheServer.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("CacheServer", "MaxSessionCount", outConfig.CacheServer.MaxSessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("CacheServer", "RecvBufferSize", outConfig.CacheServer.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("CacheServer", "SocketSendBufferBytes", outConfig.CacheServer.SocketSendBufferBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheServer", "RioSendRingSizeBytes", outConfig.CacheServer.RioSendRingSizeBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheServer", "PacketKey", outConfig.CacheServer.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"CacheServer", "ContentsWorkerThreadCount", outConfig.CacheServer.ContentsWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheServer", "PlayerCacheShardCount", outConfig.CacheServer.PlayerCacheShardCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt64("CacheServer", "MailboxCapacity", outConfig.CacheServer.MailboxCapacity, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheServer", "RpcServerInstanceId", outConfig.CacheServer.RpcServerInstanceId, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("CacheServer", "GameDataDirectory", outConfig.CacheServer.GameDataDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("CacheServer", "DatabaseEnabled", outConfig.CacheServer.DatabaseEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheServer",
				"ReplicaReconnectCooldownMilliseconds",
				outConfig.CacheServer.ReplicaReconnectCooldownMilliseconds,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum(
				"CacheServer", "LogMinimumLevel", kCacheServerLogMinimumLevelEnumValues, outConfig.CacheServer.LogMinimumLevel, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("CacheServer", "LogOutputDirectory", outConfig.CacheServer.LogOutputDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("CacheServer", "LogConsoleEnabled", outConfig.CacheServer.LogConsoleEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("CacheServer", "LogFileEnabled", outConfig.CacheServer.LogFileEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("CacheServer", "LogIncludeThreadId", outConfig.CacheServer.LogIncludeThreadId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("GameDatabase", "Password", outConfig.GameDatabase.Password, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GamePrimary", "Host", outConfig.GamePrimary.Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("GamePrimary", "Port", outConfig.GamePrimary.Port, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GamePrimary", "User", outConfig.GamePrimary.User, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GamePrimary", "Database", outConfig.GamePrimary.Database, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("GamePrimary", "ConnectTimeoutSeconds", outConfig.GamePrimary.ConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("GameReplica1", "Enabled", outConfig.GameReplica1.Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica1", "Host", outConfig.GameReplica1.Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("GameReplica1", "Port", outConfig.GameReplica1.Port, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica1", "User", outConfig.GameReplica1.User, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica1", "Database", outConfig.GameReplica1.Database, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("GameReplica1", "ConnectTimeoutSeconds", outConfig.GameReplica1.ConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("GameReplica2", "Enabled", outConfig.GameReplica2.Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica2", "Host", outConfig.GameReplica2.Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("GameReplica2", "Port", outConfig.GameReplica2.Port, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica2", "User", outConfig.GameReplica2.User, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("GameReplica2", "Database", outConfig.GameReplica2.Database, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("GameReplica2", "ConnectTimeoutSeconds", outConfig.GameReplica2.ConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"CachePolicy", "GameOwnerLeaseMilliseconds", outConfig.CachePolicy.GameOwnerLeaseMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CachePolicy", "IdleEvictionMilliseconds", outConfig.CachePolicy.IdleEvictionMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"CachePolicy", "MaintenanceIntervalMilliseconds", outConfig.CachePolicy.MaintenanceIntervalMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"CachePolicy", "RevokeTimeoutMilliseconds", outConfig.CachePolicy.RevokeTimeoutMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool(
				"FaultInjection", "CreditBeforeDatabaseTransaction", outConfig.FaultInjection.CreditBeforeDatabaseTransaction, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool(
				"FaultInjection", "CreditAfterCommitDisconnect", outConfig.FaultInjection.CreditAfterCommitDisconnect, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("FaultInjection",
				"CreditBeforeDatabaseDelayMilliseconds",
				outConfig.FaultInjection.CreditBeforeDatabaseDelayMilliseconds,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("FaultInjection",
				"CreditAfterCommitDelayMilliseconds",
				outConfig.FaultInjection.CreditAfterCommitDelayMilliseconds,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("Debug", "RunSeconds", outConfig.Debug.RunSeconds, outError))
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
