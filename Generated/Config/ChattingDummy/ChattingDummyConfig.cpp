#include "ChattingDummyClientPch.h"

#include "Generated/Config/ChattingDummy/ChattingDummyConfig.h"
#include "Foundation/Config/ConfigTypes.h"
#include "Foundation/Config/FConfigFileLoader.h"
#include "Foundation/Config/FConfigValueReader.h"

#include <array>
#include <string_view>

namespace Generated::Config::ChattingDummy
{
	constexpr std::array<Foundation::Config::SConfigEnumValue<ERoomSelectionMode>, 3> kChattingDummyRoomSelectionModeEnumValues = {
		{{"Random", ERoomSelectionMode::Random}, {"RoundRobin", ERoomSelectionMode::RoundRobin}, {"Hotspot", ERoomSelectionMode::Hotspot}}};

	bool FChattingDummyConfigLoader::LoadFromFile(
		const std::filesystem::path& filePath,
		FChattingDummyConfigDocument& outConfig,
		std::string& outError)
	{
		Foundation::Config::SConfigDocument document{};
		if (!Foundation::Config::FConfigFileLoader::LoadYamlFile(filePath, document, outError))
		{
			return false;
		}

		Foundation::Config::FConfigValueReader reader(document);

		constexpr std::array<std::string_view, 1> kKnownSections = {"ChattingDummy"};

		if (!reader.ValidateKnownSections(kKnownSections, outError))
		{
			return false;
		}

		constexpr std::array<std::string_view, 21> kChattingDummyKnownKeys = {"ServerIp",
			"Port",
			"PacketKey",
			"WorkerThreadCount",
			"SessionCount",
			"ConnectsPerSecond",
			"LoginUserIdBase",
			"RunSeconds",
			"SendIntervalMs",
			"PayloadSizeBytes",
			"HiMode",
			"RoomSelectionMode",
			"HotspotRoomIds",
			"HotspotBiasPercent",
			"RoomChangeProbabilityPercent",
			"ReconnectProbabilityPercent",
			"ReconnectDelayMs",
			"ResponseTimeoutMs",
			"ConsoleSummaryIntervalSeconds",
			"EventPollMaxCount",
			"RttCsvPath"};

		if (!reader.ValidateKnownKeys("ChattingDummy", kChattingDummyKnownKeys, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredString("ChattingDummy", "ServerIp", outConfig.ChattingDummy.ServerIp, outError))
		{
			return false;
		}

		if (!reader.ReadRequiredUInt16("ChattingDummy", "Port", outConfig.ChattingDummy.Port, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("ChattingDummy", "PacketKey", outConfig.ChattingDummy.PacketKey, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "WorkerThreadCount", outConfig.ChattingDummy.WorkerThreadCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "SessionCount", outConfig.ChattingDummy.SessionCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "ConnectsPerSecond", outConfig.ChattingDummy.ConnectsPerSecond, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalUInt32("ChattingDummy", "LoginUserIdBase", outConfig.ChattingDummy.LoginUserIdBase, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "RunSeconds", outConfig.ChattingDummy.RunSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "SendIntervalMs", outConfig.ChattingDummy.SendIntervalMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "PayloadSizeBytes", outConfig.ChattingDummy.PayloadSizeBytes, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalBool("ChattingDummy", "HiMode", outConfig.ChattingDummy.HiMode, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalEnum("ChattingDummy",
				"RoomSelectionMode",
				kChattingDummyRoomSelectionModeEnumValues,
				outConfig.ChattingDummy.RoomSelectionMode,
				outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("ChattingDummy", "HotspotRoomIds", outConfig.ChattingDummy.HotspotRoomIds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "HotspotBiasPercent", outConfig.ChattingDummy.HotspotBiasPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"ChattingDummy", "RoomChangeProbabilityPercent", outConfig.ChattingDummy.RoomChangeProbabilityPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"ChattingDummy", "ReconnectProbabilityPercent", outConfig.ChattingDummy.ReconnectProbabilityPercent, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "ReconnectDelayMs", outConfig.ChattingDummy.ReconnectDelayMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "ResponseTimeoutMs", outConfig.ChattingDummy.ResponseTimeoutMs, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32(
				"ChattingDummy", "ConsoleSummaryIntervalSeconds", outConfig.ChattingDummy.ConsoleSummaryIntervalSeconds, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalInt32("ChattingDummy", "EventPollMaxCount", outConfig.ChattingDummy.EventPollMaxCount, outError))
		{
			return false;
		}

		if (!reader.ReadOptionalString("ChattingDummy", "RttCsvPath", outConfig.ChattingDummy.RttCsvPath, outError))
		{
			return false;
		}

		return true;
	}
}
