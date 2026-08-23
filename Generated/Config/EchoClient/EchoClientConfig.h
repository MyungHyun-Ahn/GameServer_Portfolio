#pragma once

namespace Generated::Config::EchoClient
{
	struct SEchoClientConfig
	{
		std::string ServerIp = "127.0.0.1";
		std::uint16_t Port = static_cast<std::uint16_t>(19000);
		std::uint32_t LoginUserIdBase = static_cast<std::uint32_t>(1000);
		std::int32_t SessionCount = static_cast<std::int32_t>(1);
		std::int32_t RequestCount = static_cast<std::int32_t>(1);
		std::int32_t PayloadSize = static_cast<std::int32_t>(16);
		std::int32_t SendChunkSize = static_cast<std::int32_t>(0);
		std::int32_t SendChunkDelayMs = static_cast<std::int32_t>(0);
		std::int32_t RecvBufferSize = static_cast<std::int32_t>(32);
		std::int32_t ResponseThreadCount = static_cast<std::int32_t>(1);
		std::int32_t ResponsesPerThread = static_cast<std::int32_t>(1);
		std::int32_t HoldSeconds = static_cast<std::int32_t>(0);
		std::int32_t IntervalMs = static_cast<std::int32_t>(1000);
		std::int32_t PacketsPerSend = static_cast<std::int32_t>(1);
		std::int32_t ReconnectProbabilityPercent = static_cast<std::int32_t>(0);
		std::int32_t ReconnectDelayMs = static_cast<std::int32_t>(100);
		std::int32_t ConnectsPerSecond = static_cast<std::int32_t>(0);
		std::int32_t WorkerThreadCount = static_cast<std::int32_t>(4);
		std::int32_t RoomChangeProbabilityPercent = static_cast<std::int32_t>(25);
		std::int32_t MaxRoomEnterRetryCount = static_cast<std::int32_t>(5);
		std::int32_t MaxRoomChangeRetryCount = static_cast<std::int32_t>(3);
		bool EnablePagePool = true;
		std::int32_t PageSize = static_cast<std::int32_t>(4096);
	};

	struct SEchoClientDebugConfig
	{
		bool Quiet = false;
		bool BootstrapTrace = false;
		std::int32_t TraceSessionIndex = static_cast<std::int32_t>(0);
		std::int32_t RecvTimeoutMs = static_cast<std::int32_t>(0);
		std::int32_t RoomListRecvTimeoutMs = static_cast<std::int32_t>(-1);
		std::int32_t EchoRecvTimeoutMs = static_cast<std::int32_t>(-1);
		std::string RttCsvPath = "";
		std::int32_t RttFlushIntervalSeconds = static_cast<std::int32_t>(60);
	};

	struct FEchoClientConfigDocument
	{
		SEchoClientConfig EchoClient;
		SEchoClientDebugConfig Debug;
	};

	class FEchoClientConfigLoader
	{
	public:
		static bool LoadFromFile(const std::filesystem::path& filePath, FEchoClientConfigDocument& outConfig, std::string& outError);
	};
}
