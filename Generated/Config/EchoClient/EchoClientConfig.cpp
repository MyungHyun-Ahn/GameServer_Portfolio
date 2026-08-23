#include "EchoClientPch.h"

#include "Generated/Config/EchoClient/EchoClientConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::EchoClient
{
	bool FEchoClientConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FEchoClientConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 2> kKnownSections = {"EchoClient", "Debug"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 23> kEchoClientKnownKeys = {"ServerIp",
			"Port",
			"LoginUserIdBase",
			"SessionCount",
			"RequestCount",
			"PayloadSize",
			"SendChunkSize",
			"SendChunkDelayMs",
			"RecvBufferSize",
			"ResponseThreadCount",
			"ResponsesPerThread",
			"HoldSeconds",
			"IntervalMs",
			"PacketsPerSend",
			"ReconnectProbabilityPercent",
			"ReconnectDelayMs",
			"ConnectsPerSecond",
			"WorkerThreadCount",
			"RoomChangeProbabilityPercent",
			"MaxRoomEnterRetryCount",
			"MaxRoomChangeRetryCount",
			"EnablePagePool",
			"PageSize"};

		if (!reader.ValidateKnownKeys("EchoClient", kEchoClientKnownKeys, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 8> kDebugKnownKeys = {"Quiet",
			"BootstrapTrace",
			"TraceSessionIndex",
			"RecvTimeoutMs",
			"RoomListRecvTimeoutMs",
			"EchoRecvTimeoutMs",
			"RttCsvPath",
			"RttFlushIntervalSeconds"};

		if (!reader.ValidateKnownKeys("Debug", kDebugKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("EchoClient", "ServerIp", outConfig.EchoClient.ServerIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("EchoClient", "Port", outConfig.EchoClient.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("EchoClient", "LoginUserIdBase", outConfig.EchoClient.LoginUserIdBase, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "SessionCount", outConfig.EchoClient.SessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "RequestCount", outConfig.EchoClient.RequestCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "PayloadSize", outConfig.EchoClient.PayloadSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "SendChunkSize", outConfig.EchoClient.SendChunkSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "SendChunkDelayMs", outConfig.EchoClient.SendChunkDelayMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "RecvBufferSize", outConfig.EchoClient.RecvBufferSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "ResponseThreadCount", outConfig.EchoClient.ResponseThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "ResponsesPerThread", outConfig.EchoClient.ResponsesPerThread, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "HoldSeconds", outConfig.EchoClient.HoldSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "IntervalMs", outConfig.EchoClient.IntervalMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "PacketsPerSend", outConfig.EchoClient.PacketsPerSend, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"EchoClient", "ReconnectProbabilityPercent", outConfig.EchoClient.ReconnectProbabilityPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "ReconnectDelayMs", outConfig.EchoClient.ReconnectDelayMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "ConnectsPerSecond", outConfig.EchoClient.ConnectsPerSecond, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "WorkerThreadCount", outConfig.EchoClient.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"EchoClient", "RoomChangeProbabilityPercent", outConfig.EchoClient.RoomChangeProbabilityPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "MaxRoomEnterRetryCount", outConfig.EchoClient.MaxRoomEnterRetryCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "MaxRoomChangeRetryCount", outConfig.EchoClient.MaxRoomChangeRetryCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("EchoClient", "EnablePagePool", outConfig.EchoClient.EnablePagePool, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("EchoClient", "PageSize", outConfig.EchoClient.PageSize, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "Quiet", outConfig.Debug.Quiet, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("Debug", "BootstrapTrace", outConfig.Debug.BootstrapTrace, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "TraceSessionIndex", outConfig.Debug.TraceSessionIndex, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "RecvTimeoutMs", outConfig.Debug.RecvTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "RoomListRecvTimeoutMs", outConfig.Debug.RoomListRecvTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "EchoRecvTimeoutMs", outConfig.Debug.EchoRecvTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("Debug", "RttCsvPath", outConfig.Debug.RttCsvPath, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("Debug", "RttFlushIntervalSeconds", outConfig.Debug.RttFlushIntervalSeconds, outError))
		{
			return false;
		}

		return true;
	}
}
