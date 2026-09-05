#include "AuctionHouseServerPch.h"

#include "Generated/Config/AuctionHouseServer/AuctionHouseServerConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::AuctionHouseServer
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<EBackend>, 2> kAuctionHouseServerBackendEnumValues = {
		{{"Iocp", EBackend::Iocp}, {"Rio", EBackend::Rio}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ERioSendDispatchMode>, 2> kAuctionHouseServerRioSendDispatchModeEnumValues = {
		{{"Direct", ERioSendDispatchMode::Direct}, {"OwnerThread", ERioSendDispatchMode::OwnerThread}}};

	constexpr std::array<Foundation::Config::SConfigEnumValue<ELoggingMinimumLevel>, 4> kLoggingMinimumLevelEnumValues = {
		{{"Debug", ELoggingMinimumLevel::Debug},
			{"Info", ELoggingMinimumLevel::Info},
			{"Warn", ELoggingMinimumLevel::Warn},
			{"Error", ELoggingMinimumLevel::Error}}};

	bool FAuctionHouseServerConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FAuctionHouseServerConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 7> kKnownSections = {
			"AuctionHouseServer", "Logging", "Diagnostics", "Authentication", "CacheRpc", "AuctionDatabase", "Debug"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 18> kAuctionHouseServerKnownKeys = {"Backend",
			"RioSendDispatchMode",
			"BindIp",
			"Port",
			"WorkerThreadCount",
			"MaxSessionCount",
			"RecvBufferSize",
			"SocketSendBufferBytes",
			"RioSendRingSizeBytes",
			"EnablePagePool",
			"PageSize",
			"PacketKey",
			"RunSeconds",
			"CommandShardCount",
			"ContentsWorkerThreadCount",
			"CommandMailboxCapacity",
			"ExpirationPollMilliseconds",
			"GameDataDirectory"};

		if (!reader.ValidateKnownKeys("AuctionHouseServer", kAuctionHouseServerKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 5> kLoggingKnownKeys = {
			"MinimumLevel", "OutputDirectory", "ConsoleEnabled", "FileEnabled", "IncludeThreadId"};

		if (!reader.ValidateKnownKeys("Logging", kLoggingKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 2> kDiagnosticsKnownKeys = {"TimingMetricsFlushIntervalSeconds", "TimingCsvPath"};

		if (!reader.ValidateKnownKeys("Diagnostics", kDiagnosticsKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 8> kAuthenticationKnownKeys = {"Enabled",
			"RedisHost",
			"RedisPort",
			"RedisPassword",
			"RedisDatabase",
			"RedisConnectTimeoutMilliseconds",
			"TicketKeyPrefix",
			"ActiveLoginKeyPrefix"};

		if (!reader.ValidateKnownKeys("Authentication", kAuthenticationKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 10> kCacheRpcKnownKeys = {"Host",
			"Port",
			"LocalServerInstanceId",
			"RemoteServerInstanceId",
			"PacketKey",
			"RandomKey",
			"HandshakeTimeoutMilliseconds",
			"ReconnectMilliseconds",
			"NetworkWorkerThreadCount",
			"RecvScratchBufferSize"};

		if (!reader.ValidateKnownKeys("CacheRpc", kCacheRpcKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 20> kAuctionDatabaseKnownKeys = {"Enabled",
			"Password",
			"ReplicaReconnectCooldownMilliseconds",
			"PrimaryHost",
			"PrimaryPort",
			"PrimaryUser",
			"PrimarySchema",
			"PrimaryConnectTimeoutSeconds",
			"Replica1Enabled",
			"Replica1Host",
			"Replica1Port",
			"Replica1User",
			"Replica1Schema",
			"Replica1ConnectTimeoutSeconds",
			"Replica2Enabled",
			"Replica2Host",
			"Replica2Port",
			"Replica2User",
			"Replica2Schema",
			"Replica2ConnectTimeoutSeconds"};

		if (!reader.ValidateKnownKeys("AuctionDatabase", kAuctionDatabaseKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 4> kDebugKnownKeys = {"TestDelayShardIndex",
			"TestDelayMilliseconds",
			"FaultInjectListingRegisterAfterAuctionCommit",
			"FaultInjectBidRefundBeforeComplete"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum(
				"AuctionHouseServer", "Backend", kAuctionHouseServerBackendEnumValues, outConfig.AuctionHouseServer.Backend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("AuctionHouseServer",
				"RioSendDispatchMode",
				kAuctionHouseServerRioSendDispatchModeEnumValues,
				outConfig.AuctionHouseServer.RioSendDispatchMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionHouseServer", "BindIp", outConfig.AuctionHouseServer.BindIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("AuctionHouseServer", "Port", outConfig.AuctionHouseServer.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionHouseServer", "WorkerThreadCount", outConfig.AuctionHouseServer.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionHouseServer", "MaxSessionCount", outConfig.AuctionHouseServer.MaxSessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionHouseServer", "RecvBufferSize", outConfig.AuctionHouseServer.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"AuctionHouseServer", "SocketSendBufferBytes", outConfig.AuctionHouseServer.SocketSendBufferBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionHouseServer", "RioSendRingSizeBytes", outConfig.AuctionHouseServer.RioSendRingSizeBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("AuctionHouseServer", "EnablePagePool", outConfig.AuctionHouseServer.EnablePagePool, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionHouseServer", "PageSize", outConfig.AuctionHouseServer.PageSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionHouseServer", "PacketKey", outConfig.AuctionHouseServer.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("AuctionHouseServer", "RunSeconds", outConfig.AuctionHouseServer.RunSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionHouseServer", "CommandShardCount", outConfig.AuctionHouseServer.CommandShardCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"AuctionHouseServer", "ContentsWorkerThreadCount", outConfig.AuctionHouseServer.ContentsWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt64(
				"AuctionHouseServer", "CommandMailboxCapacity", outConfig.AuctionHouseServer.CommandMailboxCapacity, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionHouseServer", "ExpirationPollMilliseconds", outConfig.AuctionHouseServer.ExpirationPollMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionHouseServer", "GameDataDirectory", outConfig.AuctionHouseServer.GameDataDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("Logging", "MinimumLevel", kLoggingMinimumLevelEnumValues, outConfig.Logging.MinimumLevel, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("Logging", "OutputDirectory", outConfig.Logging.OutputDirectory, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Logging", "ConsoleEnabled", outConfig.Logging.ConsoleEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Logging", "FileEnabled", outConfig.Logging.FileEnabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Logging", "IncludeThreadId", outConfig.Logging.IncludeThreadId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"Diagnostics", "TimingMetricsFlushIntervalSeconds", outConfig.Diagnostics.TimingMetricsFlushIntervalSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("Diagnostics", "TimingCsvPath", outConfig.Diagnostics.TimingCsvPath, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Authentication", "Enabled", outConfig.Authentication.Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("Authentication", "RedisHost", outConfig.Authentication.RedisHost, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("Authentication", "RedisPort", outConfig.Authentication.RedisPort, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("Authentication", "RedisPassword", outConfig.Authentication.RedisPassword, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Authentication", "RedisDatabase", outConfig.Authentication.RedisDatabase, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"Authentication", "RedisConnectTimeoutMilliseconds", outConfig.Authentication.RedisConnectTimeoutMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("Authentication", "TicketKeyPrefix", outConfig.Authentication.TicketKeyPrefix, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("Authentication", "ActiveLoginKeyPrefix", outConfig.Authentication.ActiveLoginKeyPrefix, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("CacheRpc", "Host", outConfig.CacheRpc.Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("CacheRpc", "Port", outConfig.CacheRpc.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "LocalServerInstanceId", outConfig.CacheRpc.LocalServerInstanceId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "RemoteServerInstanceId", outConfig.CacheRpc.RemoteServerInstanceId, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "PacketKey", outConfig.CacheRpc.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "RandomKey", outConfig.CacheRpc.RandomKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"CacheRpc", "HandshakeTimeoutMilliseconds", outConfig.CacheRpc.HandshakeTimeoutMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "ReconnectMilliseconds", outConfig.CacheRpc.ReconnectMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("CacheRpc", "NetworkWorkerThreadCount", outConfig.CacheRpc.NetworkWorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt64("CacheRpc", "RecvScratchBufferSize", outConfig.CacheRpc.RecvScratchBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("AuctionDatabase", "Enabled", outConfig.AuctionDatabase.Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("AuctionDatabase", "Password", outConfig.AuctionDatabase.Password, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("AuctionDatabase",
				"ReplicaReconnectCooldownMilliseconds",
				outConfig.AuctionDatabase.ReplicaReconnectCooldownMilliseconds,
				outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "PrimaryHost", outConfig.AuctionDatabase.PrimaryHost, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("AuctionDatabase", "PrimaryPort", outConfig.AuctionDatabase.PrimaryPort, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "PrimaryUser", outConfig.AuctionDatabase.PrimaryUser, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "PrimarySchema", outConfig.AuctionDatabase.PrimarySchema, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDatabase", "PrimaryConnectTimeoutSeconds", outConfig.AuctionDatabase.PrimaryConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("AuctionDatabase", "Replica1Enabled", outConfig.AuctionDatabase.Replica1Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica1Host", outConfig.AuctionDatabase.Replica1Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("AuctionDatabase", "Replica1Port", outConfig.AuctionDatabase.Replica1Port, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica1User", outConfig.AuctionDatabase.Replica1User, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica1Schema", outConfig.AuctionDatabase.Replica1Schema, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDatabase", "Replica1ConnectTimeoutSeconds", outConfig.AuctionDatabase.Replica1ConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("AuctionDatabase", "Replica2Enabled", outConfig.AuctionDatabase.Replica2Enabled, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica2Host", outConfig.AuctionDatabase.Replica2Host, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("AuctionDatabase", "Replica2Port", outConfig.AuctionDatabase.Replica2Port, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica2User", outConfig.AuctionDatabase.Replica2User, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("AuctionDatabase", "Replica2Schema", outConfig.AuctionDatabase.Replica2Schema, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32(
				"AuctionDatabase", "Replica2ConnectTimeoutSeconds", outConfig.AuctionDatabase.Replica2ConnectTimeoutSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "TestDelayShardIndex", outConfig.Debug.TestDelayShardIndex, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("Debug", "TestDelayMilliseconds", outConfig.Debug.TestDelayMilliseconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug",
				"FaultInjectListingRegisterAfterAuctionCommit",
				outConfig.Debug.FaultInjectListingRegisterAfterAuctionCommit,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool(
				"Debug", "FaultInjectBidRefundBeforeComplete", outConfig.Debug.FaultInjectBidRefundBeforeComplete, outError))
		{
			return false;
		}

		return true;
	}
}
