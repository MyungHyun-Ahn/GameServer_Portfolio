#pragma once

namespace Generated::Config::ChattingDummy
{
	enum class ERoomSelectionMode
	{
		Random,
		RoundRobin,
		Hotspot
	};

	struct SChattingDummyConfig
	{
		std::string ServerIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19100);
		std::uint32_t PacketKey = static_cast<std::uint32_t>(55);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(4);
		std::int32_t SessionCount = static_cast<std::int32_t>(100);
		std::int32_t ConnectsPerSecond = static_cast<std::int32_t>(50);
		std::uint32_t LoginUserIdBase = static_cast<std::uint32_t>(100000);
		std::int32_t RunSeconds = static_cast<std::int32_t>(60);
		std::int32_t SendIntervalMs = static_cast<std::int32_t>(1000);
		std::int32_t PayloadSizeBytes = static_cast<std::int32_t>(1024);
		bool HiMode = false;
		ERoomSelectionMode RoomSelectionMode = ERoomSelectionMode::Random;
		std::string HotspotRoomIds = "77";
		std::int32_t HotspotBiasPercent = static_cast<std::int32_t>(80);
		std::int32_t RoomChangeProbabilityPercent = static_cast<std::int32_t>(0);
		std::int32_t ReconnectProbabilityPercent = static_cast<std::int32_t>(0);
		std::int32_t ReconnectDelayMs = static_cast<std::int32_t>(100);
		std::int32_t ResponseTimeoutMs = static_cast<std::int32_t>(5000);
		std::int32_t ConsoleSummaryIntervalSeconds = static_cast<std::int32_t>(5);
		std::int32_t EventPollMaxCount = static_cast<std::int32_t>(256);
		std::string RttCsvPath = "";
	};

	struct FChattingDummyConfigDocument
	{
		SChattingDummyConfig ChattingDummy;
	};

	class FChattingDummyConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath, FChattingDummyConfigDocument& outConfig, std::string& outError);
	};
}
