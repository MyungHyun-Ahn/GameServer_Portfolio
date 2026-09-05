#include "EchoClientPch.h"

#include "Foundation/Diagnostics/Rtt/FRttCsvLogger.h"
#include "Foundation/Diagnostics/Rtt/FRttMetricsRuntime.h"
#include "Foundation/Diagnostics/Rtt/FRttThreadLocalCollector.h"
#include "Crypto/FDefaultPacketCipher.h"
#include "EchoServer/Contents/Room/RoomFlowTypes.h"
#include "Generated/Config/EchoClient/EchoClientConfig.h"
#include "Generated/Packets/Cpp/Chat/ChatPackets.h"
#include "Generated/Packets/Cpp/Echo/EchoPackets.h"
#include "Generated/Packets/Cpp/Login/LoginPackets.h"
#include "Packet/Buffer/FPacketBuffer.h"
#include "Packet/Framing/FDefaultPacketFramer.h"
#include "Packet/Framing/PacketTypes.h"
#include "Packet/Serialization/FPacketSerialization.h"
#include "Packet/View/FPacketView.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace
{
	constexpr std::uint8_t kPacketKey = 0x37;
	std::mutex g_consoleOutputMutex;

	struct SClientOptions
	{
		std::string serverIp = "127.0.0.1";
		std::uint16_t port = 19000;
		std::uint32_t loginUserIdBase = 1000;
		int sessionCount = 1;
		int requestCount = 1;
		int payloadSize = 9;
		int sendChunkSize = 0;
		int sendChunkDelayMs = 0;
		int recvBufferSize = 32;
		int responseThreadCount = 1;
		int responsesPerThread = 1;
		int holdSeconds = 0;
		int intervalMs = 1000;
		int packetsPerSend = 1;
		int reconnectProbabilityPercent = 0;
		int reconnectDelayMs = 100;
		int connectsPerSecond = 0;
		int workerThreadCount = 4;
		int recvTimeoutMs = 0;
		int roomListRecvTimeoutMs = -1;
		int echoRecvTimeoutMs = -1;
		std::string rttCsvPath;
		int rttFlushIntervalSeconds = 60;
		int roomChangeProbabilityPercent = 25;
		int maxRoomEnterRetryCount = 5;
		int maxRoomChangeRetryCount = 3;
		int traceSessionIndex = 0;
		bool enablePagePool = true;
		int pageSize = 4096;
		bool bootstrapTrace = false;
		bool verbose = true;
	};

	struct SSessionResult
	{
		bool succeeded = false;
		int receivedResponseCount = 0;
		std::string errorMessage;
	};

	struct SRoomCandidate
	{
		std::uint32_t roomId = 0;
		std::string roomName;
		std::uint32_t participantCount = 0;
		std::uint32_t capacity = 0;
		bool joinable = false;
	};

	enum class ERttStage : std::uint8_t
	{
		LoginResponse = 0,
		RoomList,
		RoomEnter,
		EchoResponse,
		RoomChangeList,
		RoomChange,
		Count
	};

	std::filesystem::path GetExecutableDirectory(
		const char* argv0)
	{
		if (argv0 == nullptr || *argv0 == '\0')
		{
			return std::filesystem::current_path();
		}

		return std::filesystem::absolute(std::filesystem::path(argv0)).parent_path();
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

	std::filesystem::path ResolveDefaultEchoClientConfigPath(
		const std::filesystem::path& executableDirectory)
	{
		const std::filesystem::path localPath = executableDirectory / "Config" / "Client" / "EchoClient.yaml";
		if (std::filesystem::exists(localPath))
		{
			return localPath;
		}

		return executableDirectory.parent_path() / "Config" / "Client" / "EchoClient.yaml";
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

	void ApplyEchoClientConfigDocument(
		const Generated::Config::EchoClient::FEchoClientConfigDocument& configDocument,
		const std::filesystem::path& executableDirectory,
		SClientOptions& outOptions)
	{
		outOptions.serverIp = configDocument.EchoClient.ServerIp;
		outOptions.port = configDocument.EchoClient.Port;
		outOptions.loginUserIdBase = configDocument.EchoClient.LoginUserIdBase;
		outOptions.sessionCount = std::max(1, configDocument.EchoClient.SessionCount);
		outOptions.requestCount = std::max(1, configDocument.EchoClient.RequestCount);
		outOptions.payloadSize = std::max(1, configDocument.EchoClient.PayloadSize);
		outOptions.sendChunkSize = std::max(0, configDocument.EchoClient.SendChunkSize);
		outOptions.sendChunkDelayMs = std::max(0, configDocument.EchoClient.SendChunkDelayMs);
		outOptions.recvBufferSize = std::max(1, configDocument.EchoClient.RecvBufferSize);
		outOptions.responseThreadCount = std::max(1, configDocument.EchoClient.ResponseThreadCount);
		outOptions.responsesPerThread = std::max(1, configDocument.EchoClient.ResponsesPerThread);
		outOptions.holdSeconds = std::max(0, configDocument.EchoClient.HoldSeconds);
		outOptions.intervalMs = std::max(0, configDocument.EchoClient.IntervalMs);
		outOptions.packetsPerSend = std::max(1, configDocument.EchoClient.PacketsPerSend);
		outOptions.reconnectProbabilityPercent = std::clamp(configDocument.EchoClient.ReconnectProbabilityPercent, 0, 100);
		outOptions.reconnectDelayMs = std::max(0, configDocument.EchoClient.ReconnectDelayMs);
		outOptions.connectsPerSecond = std::max(0, configDocument.EchoClient.ConnectsPerSecond);
		outOptions.workerThreadCount = std::max(1, configDocument.EchoClient.WorkerThreadCount);
		outOptions.roomChangeProbabilityPercent = std::clamp(configDocument.EchoClient.RoomChangeProbabilityPercent, 0, 100);
		outOptions.maxRoomEnterRetryCount = std::max(1, configDocument.EchoClient.MaxRoomEnterRetryCount);
		outOptions.maxRoomChangeRetryCount = std::max(1, configDocument.EchoClient.MaxRoomChangeRetryCount);
		outOptions.enablePagePool = configDocument.EchoClient.EnablePagePool;
		outOptions.pageSize = std::max(1, configDocument.EchoClient.PageSize);

		outOptions.verbose = !configDocument.Debug.Quiet;
		outOptions.bootstrapTrace = configDocument.Debug.BootstrapTrace;
		outOptions.traceSessionIndex = std::max(0, configDocument.Debug.TraceSessionIndex);
		outOptions.recvTimeoutMs = std::max(0, configDocument.Debug.RecvTimeoutMs);
		outOptions.roomListRecvTimeoutMs = configDocument.Debug.RoomListRecvTimeoutMs;
		outOptions.echoRecvTimeoutMs = configDocument.Debug.EchoRecvTimeoutMs;
		outOptions.rttFlushIntervalSeconds = std::max(1, configDocument.Debug.RttFlushIntervalSeconds);

		const std::filesystem::path configuredRttCsvPath = ResolveConfiguredPath(executableDirectory, configDocument.Debug.RttCsvPath);
		outOptions.rttCsvPath = configuredRttCsvPath.empty() ? std::string() : configuredRttCsvPath.string();
	}

	bool TryParseInt(
		const char* valueText,
		int& outValue)
	{
		if (valueText == nullptr)
		{
			return false;
		}

		char* parseEnd = nullptr;
		const long parsedValue = std::strtol(valueText, &parseEnd, 10);
		if (parseEnd == valueText || *parseEnd != '\0')
		{
			return false;
		}

		outValue = static_cast<int>(parsedValue);
		return true;
	}

	bool ParseArguments(
		int argc,
		char* argv[],
		SClientOptions& outOptions)
	{
		for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
		{
			const std::string argument = argv[argumentIndex];
			if (argument == "--server-ip" && argumentIndex + 1 < argc)
			{
				outOptions.serverIp = argv[++argumentIndex];
			}
			else if (argument == "--port" && argumentIndex + 1 < argc)
			{
				int parsedValue = 0;
				if (!TryParseInt(argv[++argumentIndex], parsedValue) || parsedValue <= 0 || parsedValue > 65535)
				{
					return false;
				}

				outOptions.port = static_cast<std::uint16_t>(parsedValue);
			}
			else if (argument == "--login-userid-base" && argumentIndex + 1 < argc)
			{
				int parsedValue = 0;
				if (!TryParseInt(argv[++argumentIndex], parsedValue) || parsedValue <= 0)
				{
					return false;
				}

				outOptions.loginUserIdBase = static_cast<std::uint32_t>(parsedValue);
			}
			else if (argument == "--count" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.requestCount) || outOptions.requestCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--payload-size" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.payloadSize) || outOptions.payloadSize <= 0)
				{
					return false;
				}
			}
			else if (argument == "--send-chunk-size" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.sendChunkSize) || outOptions.sendChunkSize < 0)
				{
					return false;
				}
			}
			else if (argument == "--send-chunk-delay-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.sendChunkDelayMs) || outOptions.sendChunkDelayMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--recv-buffer-size" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.recvBufferSize) || outOptions.recvBufferSize <= 0)
				{
					return false;
				}
			}
			else if (argument == "--sessions" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.sessionCount) || outOptions.sessionCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--response-thread-count" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.responseThreadCount) || outOptions.responseThreadCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--responses-per-thread" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.responsesPerThread) || outOptions.responsesPerThread <= 0)
				{
					return false;
				}
			}
			else if (argument == "--hold-seconds" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.holdSeconds) || outOptions.holdSeconds < 0)
				{
					return false;
				}
			}
			else if (argument == "--interval-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.intervalMs) || outOptions.intervalMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--packets-per-send" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.packetsPerSend) || outOptions.packetsPerSend <= 0)
				{
					return false;
				}
			}
			else if (argument == "--reconnect-probability-percent" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.reconnectProbabilityPercent) ||
					outOptions.reconnectProbabilityPercent < 0 || outOptions.reconnectProbabilityPercent > 100)
				{
					return false;
				}
			}
			else if (argument == "--reconnect-delay-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.reconnectDelayMs) || outOptions.reconnectDelayMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--connects-per-second" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.connectsPerSecond) || outOptions.connectsPerSecond < 0)
				{
					return false;
				}
			}
			else if (argument == "--worker-thread-count" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.workerThreadCount) || outOptions.workerThreadCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--recv-timeout-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.recvTimeoutMs) || outOptions.recvTimeoutMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--room-list-recv-timeout-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.roomListRecvTimeoutMs) || outOptions.roomListRecvTimeoutMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--echo-recv-timeout-ms" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.echoRecvTimeoutMs) || outOptions.echoRecvTimeoutMs < 0)
				{
					return false;
				}
			}
			else if (argument == "--rtt-csv-path" && argumentIndex + 1 < argc)
			{
				outOptions.rttCsvPath = argv[++argumentIndex];
			}
			else if (argument == "--rtt-flush-interval-seconds" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.rttFlushIntervalSeconds) || outOptions.rttFlushIntervalSeconds <= 0)
				{
					return false;
				}
			}
			else if (argument == "--room-change-probability-percent" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.roomChangeProbabilityPercent) ||
					outOptions.roomChangeProbabilityPercent < 0 || outOptions.roomChangeProbabilityPercent > 100)
				{
					return false;
				}
			}
			else if (argument == "--max-room-enter-retries" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.maxRoomEnterRetryCount) || outOptions.maxRoomEnterRetryCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--max-room-change-retries" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.maxRoomChangeRetryCount) || outOptions.maxRoomChangeRetryCount <= 0)
				{
					return false;
				}
			}
			else if (argument == "--trace-session-index" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.traceSessionIndex) || outOptions.traceSessionIndex < 0)
				{
					return false;
				}
			}
			else if (argument == "--config" && argumentIndex + 1 < argc)
			{
				++argumentIndex;
			}
			else if (argument == "--disable-page-pool")
			{
				outOptions.enablePagePool = false;
			}
			else if (argument == "--page-size" && argumentIndex + 1 < argc)
			{
				if (!TryParseInt(argv[++argumentIndex], outOptions.pageSize) || outOptions.pageSize <= 0)
				{
					return false;
				}
			}
			else if (argument == "--quiet")
			{
				outOptions.verbose = false;
			}
			else if (argument == "--bootstrap-trace")
			{
				outOptions.bootstrapTrace = true;
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	int ResolveStageRecvTimeoutMs(
		const SClientOptions& options,
		const char* stageName)
	{
		if (stageName != nullptr)
		{
			const std::string_view stage(stageName);
			if ((stage == "room-list" || stage == "room-change-list") && options.roomListRecvTimeoutMs >= 0)
			{
				return options.roomListRecvTimeoutMs;
			}

			if (stage == "echo-response" && options.echoRecvTimeoutMs >= 0)
			{
				return options.echoRecvTimeoutMs;
			}
		}

		return options.recvTimeoutMs;
	}

	Foundation::Diagnostics::FRttStageIndex ToRttStageIndex(
		const ERttStage stage)
	{
		return static_cast<Foundation::Diagnostics::FRttStageIndex>(stage);
	}

	Foundation::Diagnostics::SRttPendingRequest MakePendingRequest(
		const ERttStage stage,
		const int sessionIndex)
	{
		Foundation::Diagnostics::SRttPendingRequest pendingRequest{};
		pendingRequest.stageIndex = ToRttStageIndex(stage);
		pendingRequest.sessionIndex = sessionIndex;
		pendingRequest.sentSteady = std::chrono::steady_clock::now();
		pendingRequest.sentSystem = std::chrono::system_clock::now();
		return pendingRequest;
	}

	Foundation::Diagnostics::SRttMetricsConfig BuildRttMetricsConfig(
		const SClientOptions& options)
	{
		Foundation::Diagnostics::SRttMetricsConfig config{};
		config.flushIntervalSeconds = options.rttFlushIntervalSeconds;
		config.stageNames = {"login-response", "room-list", "room-enter", "echo-response", "room-change-list", "room-change"};
		return config;
	}

	using FRttMetricsRuntime = Foundation::Diagnostics::FRttMetricsRuntime;
	using FRttThreadLocalCollector = Foundation::Diagnostics::FRttThreadLocalCollector;
	using FRttCsvLogger = Foundation::Diagnostics::FRttCsvLogger;
	using SRttPendingRequest = Foundation::Diagnostics::SRttPendingRequest;

	bool WaitUntilSocketReadable(
		SOCKET clientSocket,
		int timeoutMs)
	{
		if (timeoutMs <= 0)
		{
			return true;
		}

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(clientSocket, &readSet);

		timeval timeout{};
		timeout.tv_sec = timeoutMs / 1000;
		timeout.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
		const int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
		return selectResult > 0 && FD_ISSET(clientSocket, &readSet);
	}

	std::string BuildRequestMessage(
		int sessionIndex,
		int requestIndex,
		int payloadSize)
	{
		std::ostringstream messageBuilder;
		messageBuilder << "echo-s" << sessionIndex << "-r" << requestIndex;
		std::string message = messageBuilder.str();
		if (payloadSize <= static_cast<int>(message.size()))
		{
			// Even with a small payload budget, keep the per-session request key unique.
			std::ostringstream compactBuilder;
			compactBuilder << 'e' << std::uppercase << std::hex << std::setw(std::max(1, payloadSize - 1)) << std::setfill('0')
						   << static_cast<std::uint32_t>(requestIndex);
			message = compactBuilder.str();
			if (static_cast<int>(message.size()) > payloadSize)
			{
				message = message.substr(static_cast<std::size_t>(message.size() - payloadSize));
			}
			return message;
		}

		while (static_cast<int>(message.size()) < payloadSize)
		{
			const char padCharacter = static_cast<char>('a' + (requestIndex % 26));
			message.push_back(padCharacter);
		}

		return message;
	}

	bool SendFully(
		SOCKET socketHandle,
		const char* buffer,
		int length)
	{
		int totalSent = 0;
		while (totalSent < length)
		{
			const int sentBytes = send(socketHandle, buffer + totalSent, length - totalSent, 0);
			if (sentBytes == SOCKET_ERROR || sentBytes == 0)
			{
				return false;
			}

			totalSent += sentBytes;
		}

		return true;
	}

	bool SendPacketWithOptionalChunking(
		SOCKET socketHandle,
		const std::vector<char>& packetBuffer,
		int chunkSize,
		int chunkDelayMs)
	{
		if (chunkSize <= 0 || chunkSize >= static_cast<int>(packetBuffer.size()))
		{
			return SendFully(socketHandle, packetBuffer.data(), static_cast<int>(packetBuffer.size()));
		}

		int offset = 0;
		while (offset < static_cast<int>(packetBuffer.size()))
		{
			const int bytesToSend = std::min(chunkSize, static_cast<int>(packetBuffer.size()) - offset);
			if (!SendFully(socketHandle, packetBuffer.data() + offset, bytesToSend))
			{
				return false;
			}

			offset += bytesToSend;
			if (offset < static_cast<int>(packetBuffer.size()) && chunkDelayMs > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(chunkDelayMs));
			}
		}

		return true;
	}

	std::vector<std::string> BuildExpectedResponseMessages(
		const std::string& requestMessage,
		const SClientOptions& options)
	{
		if (options.responseThreadCount == 1 && options.responsesPerThread == 1)
		{
			return {requestMessage};
		}

		std::vector<std::string> expectedResponses;
		expectedResponses.reserve(static_cast<std::size_t>(options.responseThreadCount * options.responsesPerThread));
		for (int threadIndex = 0; threadIndex < options.responseThreadCount; ++threadIndex)
		{
			for (int responseIndex = 0; responseIndex < options.responsesPerThread; ++responseIndex)
			{
				std::ostringstream responseBuilder;
				responseBuilder << requestMessage << "|t=" << threadIndex << "|r=" << responseIndex;
				expectedResponses.push_back(responseBuilder.str());
			}
		}

		return expectedResponses;
	}

	bool TryConnectSocket(
		const SClientOptions& options,
		SOCKET& outSocket,
		std::string& outErrorMessage)
	{
		outSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (outSocket == INVALID_SOCKET)
		{
			outErrorMessage = "socket creation failed.";
			return false;
		}

		sockaddr_in serverAddress{};
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(options.port);
		InetPtonA(AF_INET, options.serverIp.c_str(), &serverAddress.sin_addr);

		if (connect(outSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR)
		{
			std::ostringstream oss;
			oss << "connect failed: " << WSAGetLastError();
			outErrorMessage = oss.str();
			closesocket(outSocket);
			outSocket = INVALID_SOCKET;
			return false;
		}

		if (options.recvTimeoutMs > 0)
		{
			const DWORD timeoutMs = static_cast<DWORD>(options.recvTimeoutMs);
			if (setsockopt(outSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) ==
				SOCKET_ERROR)
			{
				std::ostringstream oss;
				oss << "setsockopt(SO_RCVTIMEO) failed: " << WSAGetLastError();
				outErrorMessage = oss.str();
				closesocket(outSocket);
				outSocket = INVALID_SOCKET;
				return false;
			}
		}

		return true;
	}

	bool TryConnectSocketOverlapped(
		const SClientOptions& options,
		SOCKET& outSocket,
		std::string& outErrorMessage)
	{
		outSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (outSocket == INVALID_SOCKET)
		{
			outErrorMessage = "overlapped socket creation failed.";
			return false;
		}

		sockaddr_in serverAddress{};
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(options.port);
		InetPtonA(AF_INET, options.serverIp.c_str(), &serverAddress.sin_addr);

		if (connect(outSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR)
		{
			std::ostringstream oss;
			oss << "connect failed: " << WSAGetLastError();
			outErrorMessage = oss.str();
			closesocket(outSocket);
			outSocket = INVALID_SOCKET;
			return false;
		}

		return true;
	}

	bool ShouldReconnect(
		const SClientOptions& options,
		std::mt19937& randomEngine)
	{
		if (options.reconnectProbabilityPercent <= 0)
		{
			return false;
		}

		std::uniform_int_distribution<int> distribution(1, 100);
		return distribution(randomEngine) <= options.reconnectProbabilityPercent;
	}

	bool ShouldAttemptRoomChange(
		const SClientOptions& options,
		std::mt19937& randomEngine)
	{
		if (options.roomChangeProbabilityPercent <= 0)
		{
			return false;
		}

		std::uniform_int_distribution<int> distribution(1, 100);
		return distribution(randomEngine) <= options.roomChangeProbabilityPercent;
	}

	void TraceSession(
		const SClientOptions& options,
		int sessionIndex,
		const std::string& message)
	{
		if (!options.bootstrapTrace || sessionIndex != options.traceSessionIndex)
		{
			return;
		}

		const std::lock_guard<std::mutex> lock(g_consoleOutputMutex);
		std::cerr << "[trace][session " << sessionIndex << "] " << message << "\n";
	}

	std::vector<char> BuildContentPacketBuffer(
		NetworkLib::Crypto::FDefaultPacketCipher& packetCipher,
		NetworkLib::Packet::Framing::FDefaultPacketFramer& packetFramer,
		const NetworkLib::Packet::Serialization::IContentPacket& packet,
		const std::uint8_t randomKey)
	{
		std::vector<char> serializedPayload = NetworkLib::Packet::Serialization::SerializeContentPacket(packet);
		packetCipher.Encode(serializedPayload.data(), static_cast<int>(serializedPayload.size()), randomKey);

		NetworkLib::Packet::Framing::SOutgoingPacket outgoingPacket{};
		outgoingPacket.randomKey = randomKey;
		outgoingPacket.checkSum = NetworkLib::Packet::Framing::CalculatePacketChecksum(
			serializedPayload.data(), static_cast<std::int32_t>(serializedPayload.size()));
		outgoingPacket.payload = serializedPayload.data();
		outgoingPacket.payloadLength = static_cast<std::int32_t>(serializedPayload.size());

		std::vector<char> outboundPacket;
		if (!packetFramer.BuildPacket(outgoingPacket, outboundPacket))
		{
			return {};
		}

		return outboundPacket;
	}

	bool SendContentPacketRequest(
		SOCKET clientSocket,
		NetworkLib::Crypto::FDefaultPacketCipher& packetCipher,
		NetworkLib::Packet::Framing::FDefaultPacketFramer& packetFramer,
		const NetworkLib::Packet::Serialization::IContentPacket& packet,
		const std::uint8_t randomKey,
		const SClientOptions& options,
		std::string& outErrorMessage)
	{
		std::vector<char> outboundPacket = BuildContentPacketBuffer(packetCipher, packetFramer, packet, randomKey);
		if (outboundPacket.empty())
		{
			outErrorMessage = "BuildPacket failed.";
			return false;
		}

		if (!SendPacketWithOptionalChunking(clientSocket, outboundPacket, options.sendChunkSize, options.sendChunkDelayMs))
		{
			std::ostringstream oss;
			oss << "send failed. error=" << WSAGetLastError();
			outErrorMessage = oss.str();
			return false;
		}

		return true;
	}

	bool TryBuildRoomCandidates(
		const Generated::Chat::FRoomListRp& responsePacket,
		std::vector<SRoomCandidate>& outCandidates)
	{
		const std::size_t roomCount = responsePacket.roomIds.size();
		if (responsePacket.roomNames.size() != roomCount || responsePacket.participantCounts.size() != roomCount ||
			responsePacket.capacities.size() != roomCount || responsePacket.joinableFlags.size() != roomCount)
		{
			return false;
		}

		outCandidates.clear();
		outCandidates.reserve(roomCount);
		for (std::size_t index = 0; index < roomCount; ++index)
		{
			outCandidates.push_back({responsePacket.roomIds[index],
				responsePacket.roomNames[index],
				responsePacket.participantCounts[index],
				responsePacket.capacities[index],
				responsePacket.joinableFlags[index] != 0});
		}

		return true;
	}

	std::vector<SRoomCandidate> BuildJoinableRoomCandidates(
		const std::vector<SRoomCandidate>& roomCandidates,
		const std::optional<std::uint32_t> excludedRoomId = std::nullopt)
	{
		std::vector<SRoomCandidate> joinableRooms;
		for (const SRoomCandidate& roomCandidate : roomCandidates)
		{
			if (!roomCandidate.joinable)
			{
				continue;
			}

			if (excludedRoomId.has_value() && roomCandidate.roomId == excludedRoomId.value())
			{
				continue;
			}

			joinableRooms.push_back(roomCandidate);
		}

		return joinableRooms;
	}

	std::optional<SRoomCandidate> PickRandomRoomCandidate(
		const std::vector<SRoomCandidate>& roomCandidates,
		std::mt19937& randomEngine)
	{
		if (roomCandidates.empty())
		{
			return std::nullopt;
		}

		std::uniform_int_distribution<std::size_t> distribution(0, roomCandidates.size() - 1);
		return roomCandidates[distribution(randomEngine)];
	}

	SSessionResult RunSingleSession(
		int sessionIndex,
		const SClientOptions& options,
		FRttMetricsRuntime* rttMetricsRuntime)
	{
		SSessionResult sessionResult{};
		FRttThreadLocalCollector rttCollector(rttMetricsRuntime);
		NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
		cipherConfig.packetKey = kPacketKey;
		NetworkLib::Crypto::FDefaultPacketCipher packetCipher(cipherConfig);
		NetworkLib::Packet::Framing::FDefaultPacketFramer packetFramer;
		const auto startTime = std::chrono::steady_clock::now();
		const auto deadline = startTime + std::chrono::seconds(options.holdSeconds > 0 ? options.holdSeconds : 0);
		int requestSequence = 0;
		std::mt19937 randomEngine(static_cast<std::uint32_t>(GetTickCount64()) ^ static_cast<std::uint32_t>(sessionIndex * 2654435761u));
		SOCKET clientSocket = INVALID_SOCKET;
		bool requiresBootstrapAfterConnect = true;
		std::optional<std::uint32_t> currentRoomId;

		while (true)
		{
			if (clientSocket == INVALID_SOCKET)
			{
				if (!TryConnectSocket(options, clientSocket, sessionResult.errorMessage))
				{
					return sessionResult;
				}

				requiresBootstrapAfterConnect = true;
				currentRoomId.reset();
			}

			std::vector<char> inboundBuffer;
			inboundBuffer.reserve(static_cast<std::size_t>(options.recvBufferSize) * 2);
			std::vector<char> recvChunk(static_cast<std::size_t>(options.recvBufferSize));
			std::unordered_map<std::string, int> expectedResponseCounts;
			std::unordered_map<std::string, SRttPendingRequest> expectedResponseMetrics;
			std::vector<char> sendBatchBuffer;
			auto tryReceiveNextContentPacket = [&](const char* stageName,
												   NetworkLib::Packet::Framing::SFramedPacket& outFramedPacket,
												   NetworkLib::Packet::View::FPacketView& outContentPacketView,
												   const std::optional<ERttStage> timeoutStage = std::nullopt,
												   const SRttPendingRequest* successPendingRequest = nullptr) -> bool
			{
				const int effectiveRecvTimeoutMs = ResolveStageRecvTimeoutMs(options, stageName);
				while (true)
				{
					if (packetFramer.TryExtractPacket(inboundBuffer, outFramedPacket))
					{
						const std::uint8_t responseChecksum = NetworkLib::Packet::Framing::CalculatePacketChecksum(
							outFramedPacket.payload.data(), static_cast<std::int32_t>(outFramedPacket.payload.size()));
						if (responseChecksum != outFramedPacket.checkSum)
						{
							sessionResult.errorMessage = "packet checksum failed.";
							return false;
						}

						packetCipher.Decode(
							outFramedPacket.payload.data(), static_cast<int>(outFramedPacket.payload.size()), outFramedPacket.randomKey);

						NetworkLib::Packet::View::FPacketView transportPacketView{};
						transportPacketView.randomKey = outFramedPacket.randomKey;
						transportPacketView.checkSum = outFramedPacket.checkSum;
						transportPacketView.payload = outFramedPacket.payload.data();
						transportPacketView.payloadLength = static_cast<std::int32_t>(outFramedPacket.payload.size());

						if (!NetworkLib::Packet::Serialization::TryParseContentPacketView(transportPacketView, outContentPacketView))
						{
							sessionResult.errorMessage = "response content header parse failed.";
							return false;
						}

						if (successPendingRequest != nullptr)
						{
							rttCollector.RecordSample(*successPendingRequest, std::chrono::system_clock::now());
						}

						return true;
					}

					if (packetFramer.HasInvalidPacketHeader(inboundBuffer))
					{
						sessionResult.errorMessage = "oversized response packet header rejected.";
						return false;
					}

					if (!WaitUntilSocketReadable(clientSocket, effectiveRecvTimeoutMs))
					{
						if (timeoutStage.has_value())
						{
							rttCollector.RecordTimeout(ToRttStageIndex(*timeoutStage), std::chrono::system_clock::now());
						}

						std::ostringstream oss;
						oss << "recv failed at stage=" << (stageName != nullptr ? stageName : "unknown") << " sessionIndex=" << sessionIndex
							<< " error=" << WSAETIMEDOUT << " (timeout)";
						sessionResult.errorMessage = oss.str();
						return false;
					}

					const int recvBytes = recv(clientSocket, recvChunk.data(), static_cast<int>(recvChunk.size()), 0);
					if (recvBytes <= 0)
					{
						const int errorCode = WSAGetLastError();
						if (errorCode == WSAETIMEDOUT && timeoutStage.has_value())
						{
							rttCollector.RecordTimeout(ToRttStageIndex(*timeoutStage), std::chrono::system_clock::now());
						}
						std::ostringstream oss;
						oss << "recv failed at stage=" << (stageName != nullptr ? stageName : "unknown") << " sessionIndex=" << sessionIndex
							<< " error=" << errorCode;
						if (errorCode == WSAETIMEDOUT)
						{
							oss << " (timeout)";
						}
						sessionResult.errorMessage = oss.str();
						return false;
					}

					inboundBuffer.insert(inboundBuffer.end(), recvChunk.begin(), recvChunk.begin() + recvBytes);
				}
			};

			if (requiresBootstrapAfterConnect)
			{
				Generated::Login::FLoginRq loginRequest;
				loginRequest.userId = options.loginUserIdBase + static_cast<std::uint32_t>(sessionIndex);
				if (!SendContentPacketRequest(clientSocket,
						packetCipher,
						packetFramer,
						loginRequest,
						static_cast<std::uint8_t>((0x21 + sessionIndex) & 0xFF),
						options,
						sessionResult.errorMessage))
				{
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				const SRttPendingRequest loginPendingRequest =
					rttCollector.BeginRequest(ToRttStageIndex(ERttStage::LoginResponse), sessionIndex);
				NetworkLib::Packet::Framing::SFramedPacket loginResponseFramedPacket{};
				NetworkLib::Packet::View::FPacketView loginResponsePacketView{};
				if (!tryReceiveNextContentPacket("login-response",
						loginResponseFramedPacket,
						loginResponsePacketView,
						ERttStage::LoginResponse,
						&loginPendingRequest))
				{
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				if (loginResponsePacketView.opcode != Generated::Login::FLoginRp::kOpcode)
				{
					std::ostringstream oss;
					oss << "unexpected login response opcode: " << loginResponsePacketView.opcode;
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				Generated::Login::FLoginRp loginResponse;
				if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(loginResponsePacketView, loginResponse))
				{
					sessionResult.errorMessage = "login response deserialize failed.";
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				if (!loginResponse.success || loginResponse.userId != loginRequest.userId)
				{
					sessionResult.errorMessage = "login validation failed.";
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				if (options.bootstrapTrace)
				{
					std::cerr << "bootstrap trace: login response ok. sessionIndex=" << sessionIndex << " userId=" << loginResponse.userId
							  << "\n";
				}

				bool roomEntered = false;
				for (int attemptIndex = 0; attemptIndex < options.maxRoomEnterRetryCount && !roomEntered; ++attemptIndex)
				{
					Generated::Chat::FRoomListRq roomListRequest;
					if (!SendContentPacketRequest(clientSocket,
							packetCipher,
							packetFramer,
							roomListRequest,
							static_cast<std::uint8_t>((0x41 + sessionIndex + attemptIndex) & 0xFF),
							options,
							sessionResult.errorMessage))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const SRttPendingRequest roomListPendingRequest =
						rttCollector.BeginRequest(ToRttStageIndex(ERttStage::RoomList), sessionIndex);
					NetworkLib::Packet::Framing::SFramedPacket roomListFramedPacket{};
					NetworkLib::Packet::View::FPacketView roomListPacketView{};
					if (!tryReceiveNextContentPacket(
							"room-list", roomListFramedPacket, roomListPacketView, ERttStage::RoomList, &roomListPendingRequest))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomListPacketView.opcode != Generated::Chat::FRoomListRp::kOpcode)
					{
						std::ostringstream oss;
						oss << "unexpected room list response opcode: " << roomListPacketView.opcode;
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					Generated::Chat::FRoomListRp roomListResponse;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(roomListPacketView, roomListResponse))
					{
						sessionResult.errorMessage = "room list response deserialize failed.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					std::vector<SRoomCandidate> roomCandidates;
					if (!TryBuildRoomCandidates(roomListResponse, roomCandidates))
					{
						sessionResult.errorMessage = "room list response validation failed.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const auto joinableRooms = BuildJoinableRoomCandidates(roomCandidates);
					const auto targetRoomCandidate = PickRandomRoomCandidate(joinableRooms, randomEngine);
					if (!targetRoomCandidate.has_value())
					{
						sessionResult.errorMessage = "no joinable room available.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					Generated::Chat::FRoomEnterRq roomEnterRequest;
					roomEnterRequest.roomId = targetRoomCandidate->roomId;
					TraceSession(options, sessionIndex, "send RoomEnterRq roomId=" + std::to_string(roomEnterRequest.roomId));
					if (!SendContentPacketRequest(clientSocket,
							packetCipher,
							packetFramer,
							roomEnterRequest,
							static_cast<std::uint8_t>((0x51 + sessionIndex + attemptIndex) & 0xFF),
							options,
							sessionResult.errorMessage))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const SRttPendingRequest roomEnterPendingRequest =
						rttCollector.BeginRequest(ToRttStageIndex(ERttStage::RoomEnter), sessionIndex);
					NetworkLib::Packet::Framing::SFramedPacket roomEnterFramedPacket{};
					NetworkLib::Packet::View::FPacketView roomEnterPacketView{};
					if (!tryReceiveNextContentPacket(
							"room-enter", roomEnterFramedPacket, roomEnterPacketView, ERttStage::RoomEnter, &roomEnterPendingRequest))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomEnterPacketView.opcode != Generated::Chat::FRoomEnterRp::kOpcode)
					{
						std::ostringstream oss;
						oss << "unexpected room enter response opcode: " << roomEnterPacketView.opcode;
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					Generated::Chat::FRoomEnterRp roomEnterResponse;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(roomEnterPacketView, roomEnterResponse))
					{
						sessionResult.errorMessage = "room enter response deserialize failed.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomEnterResponse.success)
					{
						currentRoomId = roomEnterResponse.roomId;
						TraceSession(options, sessionIndex, "recv RoomEnterRp success roomId=" + std::to_string(roomEnterResponse.roomId));
						roomEntered = true;
						if (options.bootstrapTrace)
						{
							std::cerr << "bootstrap trace: room enter ok. sessionIndex=" << sessionIndex
									  << " roomId=" << roomEnterResponse.roomId << "\n";
						}
						break;
					}

					const auto resultCode = static_cast<EchoServer::Contents::ERoomFlowResultCode>(roomEnterResponse.resultCode);
					if (!EchoServer::Contents::IsNormalRoomFlowFailure(resultCode))
					{
						std::ostringstream oss;
						oss << "room enter failed with abnormal resultCode=" << EchoServer::Contents::ToString(resultCode);
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}
				}

				if (!roomEntered)
				{
					sessionResult.errorMessage = "room enter retry exhausted.";
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				requiresBootstrapAfterConnect = false;
			}

			std::vector<std::string> pendingBatchResponseMetrics;
			for (int requestIndex = 0; requestIndex < options.requestCount; ++requestIndex)
			{
				const std::string requestMessage = BuildRequestMessage(sessionIndex, requestSequence++, options.payloadSize);
				const std::vector<std::string> expectedResponses = BuildExpectedResponseMessages(requestMessage, options);
				for (const std::string& expectedResponse : expectedResponses)
				{
					++expectedResponseCounts[expectedResponse];
					pendingBatchResponseMetrics.push_back(expectedResponse);
				}

				TraceSession(options,
					sessionIndex,
					"sent echo batch requestCount=" + std::to_string(options.requestCount) +
						" currentRoomId=" + (currentRoomId.has_value() ? std::to_string(*currentRoomId) : std::string("none")));

				Generated::Echo::FEchoRq requestPacket;
				requestPacket.SetMessageValue(requestMessage);
				std::vector<char> serializedPayload = NetworkLib::Packet::Serialization::SerializeContentPacket(requestPacket);
				const std::uint8_t requestRandomKey = static_cast<std::uint8_t>((0x61 + requestSequence + sessionIndex) & 0xFF);
				packetCipher.Encode(serializedPayload.data(), static_cast<int>(serializedPayload.size()), requestRandomKey);

				NetworkLib::Packet::Framing::SOutgoingPacket outgoingPacket{};
				outgoingPacket.randomKey = requestRandomKey;
				outgoingPacket.checkSum = NetworkLib::Packet::Framing::CalculatePacketChecksum(
					serializedPayload.data(), static_cast<std::int32_t>(serializedPayload.size()));
				outgoingPacket.payload = serializedPayload.data();
				outgoingPacket.payloadLength = static_cast<std::int32_t>(serializedPayload.size());

				std::vector<char> outboundPacket;
				if (!packetFramer.BuildPacket(outgoingPacket, outboundPacket))
				{
					std::ostringstream oss;
					oss << "BuildPacket failed for request " << requestIndex << '.';
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				sendBatchBuffer.insert(sendBatchBuffer.end(), outboundPacket.begin(), outboundPacket.end());
				const bool shouldFlush = ((requestIndex + 1) % options.packetsPerSend) == 0 || requestIndex == options.requestCount - 1;
				if (!shouldFlush)
				{
					continue;
				}

				if (!SendPacketWithOptionalChunking(clientSocket, sendBatchBuffer, options.sendChunkSize, options.sendChunkDelayMs))
				{
					std::ostringstream oss;
					oss << "send failed for request " << requestIndex << ". error=" << WSAGetLastError();
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}
				sendBatchBuffer.clear();

				const auto batchSentSteady = std::chrono::steady_clock::now();
				const auto batchSentSystem = std::chrono::system_clock::now();
				for (const std::string& expectedResponse : pendingBatchResponseMetrics)
				{
					SRttPendingRequest pendingRequest{};
					pendingRequest.stageIndex = ToRttStageIndex(ERttStage::EchoResponse);
					pendingRequest.sessionIndex = sessionIndex;
					pendingRequest.sentSteady = batchSentSteady;
					pendingRequest.sentSystem = batchSentSystem;
					expectedResponseMetrics.insert_or_assign(expectedResponse, pendingRequest);
				}
				pendingBatchResponseMetrics.clear();
			}

			const int expectedResponseCount = options.requestCount * options.responseThreadCount * options.responsesPerThread;
			int cycleReceivedResponseCount = 0;

			while (cycleReceivedResponseCount < expectedResponseCount)
			{
				NetworkLib::Packet::Framing::SFramedPacket framedPacket{};
				NetworkLib::Packet::View::FPacketView contentPacketView{};
				if (!tryReceiveNextContentPacket("echo-response", framedPacket, contentPacketView, ERttStage::EchoResponse))
				{
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				if (contentPacketView.opcode != Generated::Echo::FEchoRp::kOpcode)
				{
					std::ostringstream oss;
					oss << "unexpected opcode: " << contentPacketView.opcode;
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				Generated::Echo::FEchoRp responsePacket;
				if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(contentPacketView, responsePacket))
				{
					sessionResult.errorMessage = "response packet deserialize failed.";
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				if (!responsePacket.ContainsBorrowedViews())
				{
					sessionResult.errorMessage = "echo response should report borrowed view payload.";
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}

				const std::string responseMessage(responsePacket.GetMessageValue());
				TraceSession(options,
					sessionIndex,
					"recv EchoRp message=" + responseMessage +
						" currentRoomId=" + (currentRoomId.has_value() ? std::to_string(*currentRoomId) : std::string("none")));
				auto expectedIt = expectedResponseCounts.find(responseMessage);
				if (expectedIt == expectedResponseCounts.end() || expectedIt->second <= 0)
				{
					std::ostringstream oss;
					oss << "unexpected response=" << responseMessage;
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}
				--expectedIt->second;

				auto pendingMetricIt = expectedResponseMetrics.find(responseMessage);
				if (pendingMetricIt != expectedResponseMetrics.end())
				{
					rttCollector.RecordSample(pendingMetricIt->second, std::chrono::system_clock::now());
					expectedResponseMetrics.erase(pendingMetricIt);
				}

				if (options.verbose)
				{
					std::cout << "session[" << sessionIndex << "] response[" << sessionResult.receivedResponseCount
							  << "]: " << responseMessage << "\n";
				}

				++cycleReceivedResponseCount;
				++sessionResult.receivedResponseCount;
			}

			for (const auto& [message, remainingCount] : expectedResponseCounts)
			{
				if (remainingCount != 0)
				{
					std::ostringstream oss;
					oss << "missing response=" << message << " remaining=" << remainingCount;
					sessionResult.errorMessage = oss.str();
					closesocket(clientSocket);
					clientSocket = INVALID_SOCKET;
					return sessionResult;
				}
			}

			if (currentRoomId.has_value() && ShouldAttemptRoomChange(options, randomEngine))
			{
				for (int attemptIndex = 0; attemptIndex < options.maxRoomChangeRetryCount; ++attemptIndex)
				{
					Generated::Chat::FRoomListRq roomListRequest;
					if (!SendContentPacketRequest(clientSocket,
							packetCipher,
							packetFramer,
							roomListRequest,
							static_cast<std::uint8_t>((0x71 + sessionIndex + attemptIndex) & 0xFF),
							options,
							sessionResult.errorMessage))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const SRttPendingRequest roomChangeListPendingRequest =
						rttCollector.BeginRequest(ToRttStageIndex(ERttStage::RoomChangeList), sessionIndex);
					NetworkLib::Packet::Framing::SFramedPacket roomListFramedPacket{};
					NetworkLib::Packet::View::FPacketView roomListPacketView{};
					if (!tryReceiveNextContentPacket("room-change-list",
							roomListFramedPacket,
							roomListPacketView,
							ERttStage::RoomChangeList,
							&roomChangeListPendingRequest))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomListPacketView.opcode != Generated::Chat::FRoomListRp::kOpcode)
					{
						std::ostringstream oss;
						oss << "unexpected room list response opcode during change: " << roomListPacketView.opcode;
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					Generated::Chat::FRoomListRp roomListResponse;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(roomListPacketView, roomListResponse))
					{
						sessionResult.errorMessage = "room list response deserialize failed during change.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					std::vector<SRoomCandidate> roomCandidates;
					if (!TryBuildRoomCandidates(roomListResponse, roomCandidates))
					{
						sessionResult.errorMessage = "room list response validation failed during change.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const auto joinableRooms = BuildJoinableRoomCandidates(roomCandidates, currentRoomId);
					const auto targetRoomCandidate = PickRandomRoomCandidate(joinableRooms, randomEngine);
					if (!targetRoomCandidate.has_value())
					{
						break;
					}

					Generated::Chat::FRoomChangeRq roomChangeRequest;
					roomChangeRequest.targetRoomId = targetRoomCandidate->roomId;
					TraceSession(options,
						sessionIndex,
						"send RoomChangeRq fromRoomId=" +
							(currentRoomId.has_value() ? std::to_string(*currentRoomId) : std::string("none")) +
							" targetRoomId=" + std::to_string(roomChangeRequest.targetRoomId));
					if (!SendContentPacketRequest(clientSocket,
							packetCipher,
							packetFramer,
							roomChangeRequest,
							static_cast<std::uint8_t>((0x81 + sessionIndex + attemptIndex) & 0xFF),
							options,
							sessionResult.errorMessage))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					const SRttPendingRequest roomChangePendingRequest =
						rttCollector.BeginRequest(ToRttStageIndex(ERttStage::RoomChange), sessionIndex);
					NetworkLib::Packet::Framing::SFramedPacket roomChangeFramedPacket{};
					NetworkLib::Packet::View::FPacketView roomChangePacketView{};
					if (!tryReceiveNextContentPacket(
							"room-change", roomChangeFramedPacket, roomChangePacketView, ERttStage::RoomChange, &roomChangePendingRequest))
					{
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomChangePacketView.opcode != Generated::Chat::FRoomChangeRp::kOpcode)
					{
						std::ostringstream oss;
						oss << "unexpected room change response opcode: " << roomChangePacketView.opcode;
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					Generated::Chat::FRoomChangeRp roomChangeResponse;
					if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(roomChangePacketView, roomChangeResponse))
					{
						sessionResult.errorMessage = "room change response deserialize failed.";
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}

					if (roomChangeResponse.success)
					{
						TraceSession(options,
							sessionIndex,
							"recv RoomChangeRp success previousRoomId=" + std::to_string(roomChangeResponse.previousRoomId) +
								" currentRoomId=" + std::to_string(roomChangeResponse.currentRoomId));
						currentRoomId = roomChangeResponse.currentRoomId;
						break;
					}

					const auto resultCode = static_cast<EchoServer::Contents::ERoomFlowResultCode>(roomChangeResponse.resultCode);
					TraceSession(options,
						sessionIndex,
						"recv RoomChangeRp failure resultCode=" + std::string(EchoServer::Contents::ToString(resultCode)));
					if (!EchoServer::Contents::IsNormalRoomFlowFailure(resultCode))
					{
						std::ostringstream oss;
						oss << "room change failed with abnormal resultCode=" << EchoServer::Contents::ToString(resultCode);
						sessionResult.errorMessage = oss.str();
						closesocket(clientSocket);
						clientSocket = INVALID_SOCKET;
						return sessionResult;
					}
				}
			}

			if (options.holdSeconds <= 0)
			{
				break;
			}

			if (ShouldReconnect(options, randomEngine))
			{
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				clientSocket = INVALID_SOCKET;
				requiresBootstrapAfterConnect = true;
				currentRoomId.reset();
				if (options.reconnectDelayMs > 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(options.reconnectDelayMs));
				}
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				break;
			}

			if (options.intervalMs > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
			}
		}

		if (clientSocket != INVALID_SOCKET)
		{
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
		}

		sessionResult.succeeded = true;
		return sessionResult;
	}

	enum class EClientSessionState : std::uint8_t
	{
		None = 0,
		WaitingLoginResponse,
		WaitingRoomListForEnter,
		WaitingRoomEnterResponse,
		WaitingEchoResponses,
		WaitingRoomChangeList,
		WaitingRoomChangeResponse,
		WaitingConnect,
		WaitingInterval,
		WaitingReconnect,
		Completed,
		Failed
	};

	const char* ToString(
		const EClientSessionState state) noexcept
	{
		switch (state)
		{
			case EClientSessionState::None:
				return "None";
			case EClientSessionState::WaitingLoginResponse:
				return "WaitingLoginResponse";
			case EClientSessionState::WaitingRoomListForEnter:
				return "WaitingRoomListForEnter";
			case EClientSessionState::WaitingRoomEnterResponse:
				return "WaitingRoomEnterResponse";
			case EClientSessionState::WaitingEchoResponses:
				return "WaitingEchoResponses";
			case EClientSessionState::WaitingRoomChangeList:
				return "WaitingRoomChangeList";
			case EClientSessionState::WaitingRoomChangeResponse:
				return "WaitingRoomChangeResponse";
			case EClientSessionState::WaitingConnect:
				return "WaitingConnect";
			case EClientSessionState::WaitingInterval:
				return "WaitingInterval";
			case EClientSessionState::WaitingReconnect:
				return "WaitingReconnect";
			case EClientSessionState::Completed:
				return "Completed";
			case EClientSessionState::Failed:
				return "Failed";
			default:
				return "Unknown";
		}
	}

	enum class EClientIoOperation : std::uint8_t
	{
		Recv = 0,
		Send
	};

	enum class EClientCommandType : std::uint8_t
	{
		ContinueCycle = 0,
		Reconnect,
		Connect
	};

	struct SClientIocpSession;

	struct SClientIoContext
	{
		OVERLAPPED overlapped{};
		EClientIoOperation operation = EClientIoOperation::Recv;
		SClientIocpSession* session = nullptr;
	};

	struct SClientRecvContext final : SClientIoContext
	{
		std::vector<char> buffer;
	};

	struct SClientSendContext final : SClientIoContext
	{
	};

	struct SClientCommand
	{
		EClientCommandType type = EClientCommandType::ContinueCycle;
		int sessionIndex = -1;
	};

	struct SClientIocpSession final
	{
		int sessionIndex = 0;
		SOCKET socketHandle = INVALID_SOCKET;
		std::mutex mutex;
		SSessionResult result;
		std::mt19937 randomEngine{};
		NetworkLib::Crypto::FDefaultPacketCipher packetCipher;
		NetworkLib::Packet::Framing::FDefaultPacketFramer packetFramer;
		SClientRecvContext recvContext;
		SClientSendContext sendContext;
		std::vector<char> inboundBuffer;
		std::deque<std::vector<char>> sendQueue;
		std::vector<char> activeSendBuffer;
		std::size_t activeSendOffset = 0;
		bool sendInFlight = false;
		bool recvPosted = false;
		bool finalized = false;
		bool commandPending = false;
		EClientSessionState state = EClientSessionState::None;
		std::optional<std::uint32_t> currentRoomId;
		int requestSequence = 0;
		int roomEnterAttemptCount = 0;
		int roomChangeAttemptCount = 0;
		int expectedResponseCount = 0;
		int cycleReceivedResponseCount = 0;
		std::unordered_map<std::string, int> expectedResponseCounts;
		std::unordered_map<std::string, SRttPendingRequest> expectedResponseMetrics;
		std::optional<ERttStage> timeoutStage;
		std::optional<SRttPendingRequest> singlePendingRequest;
		std::string timeoutStageName;
		std::chrono::steady_clock::time_point timeoutDeadline{};
		std::chrono::steady_clock::time_point wakeTime{};
		std::chrono::steady_clock::time_point deadline{};
		bool holdDeadlineStarted = false;

		SClientIocpSession();
	};

	class FIocpEchoClientRuntime final
	{
	public:
		FIocpEchoClientRuntime(const SClientOptions& options, FRttMetricsRuntime* rttMetricsRuntime);
		bool Run(std::vector<SSessionResult>& outSessionResults);

	private:
		static constexpr ULONG_PTR kCommandCompletionKey = 1;
		static constexpr ULONG_PTR kShutdownCompletionKey = 2;

		void StartWorkers();
		void WaitForCompletion();
		void RequestStop();
		void JoinThreads();
		bool ConnectSession(SClientIocpSession& session, std::string& outErrorMessage);
		void SchedulerLoop();
		void WorkerLoop(FRttThreadLocalCollector& rttCollector);
		void DrainCommands(FRttThreadLocalCollector& rttCollector);
		void HandleConnectCommand(SClientIocpSession& session);
		void HandleContinueCommand(SClientIocpSession& session);
		void HandleReconnectCommand(SClientIocpSession& session);
		void HandleIoFailure(SClientIocpSession& session,
			EClientIoOperation operation,
			int errorCode,
			FRttThreadLocalCollector& rttCollector);
		void HandleRecvCompletion(SClientIocpSession& session, DWORD transferredBytes, FRttThreadLocalCollector& rttCollector);
		void HandleSendCompletion(SClientIocpSession& session, DWORD transferredBytes);
		void HandleContentPacketLocked(SClientIocpSession& session,
			const NetworkLib::Packet::View::FPacketView& contentPacketView,
			FRttThreadLocalCollector& rttCollector);
		void HandleLoginResponseLocked(SClientIocpSession& session, const NetworkLib::Packet::View::FPacketView& packetView);
		void HandleRoomListForEnterLocked(SClientIocpSession& session, const NetworkLib::Packet::View::FPacketView& packetView);
		void HandleRoomEnterResponseLocked(SClientIocpSession& session, const NetworkLib::Packet::View::FPacketView& packetView);
		void HandleEchoResponseLocked(SClientIocpSession& session,
			const NetworkLib::Packet::View::FPacketView& packetView,
			FRttThreadLocalCollector& rttCollector);
		void HandleRoomChangeListLocked(SClientIocpSession& session, const NetworkLib::Packet::View::FPacketView& packetView);
		void HandleRoomChangeResponseLocked(SClientIocpSession& session, const NetworkLib::Packet::View::FPacketView& packetView);
		void OnCycleCompletedLocked(SClientIocpSession& session);
		bool SendLoginLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool SendRoomListForEnterLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool SendRoomEnterLocked(SClientIocpSession& session, std::uint32_t roomId, std::string& outErrorMessage);
		bool SendRoomChangeListLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool SendRoomChangeLocked(SClientIocpSession& session, std::uint32_t targetRoomId, std::string& outErrorMessage);
		bool StartEchoCycleLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool QueuePacketLocked(SClientIocpSession& session,
			const NetworkLib::Packet::Serialization::IContentPacket& packet,
			std::uint8_t randomKey,
			std::string& outErrorMessage);
		bool EnqueueSendBufferLocked(SClientIocpSession& session, std::vector<char>&& buffer, std::string& outErrorMessage);
		bool StartNextSendLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool SubmitActiveSendLocked(SClientIocpSession& session, std::string& outErrorMessage);
		bool PostRecvLocked(SClientIocpSession& session, std::string& outErrorMessage);
		void SetWaitStateLocked(SClientIocpSession& session,
			EClientSessionState state,
			ERttStage stage,
			const char* stageName,
			const std::optional<SRttPendingRequest>& pendingRequest);
		void ClearWaitStateLocked(SClientIocpSession& session);
		void RefreshWaitDeadlineLocked(SClientIocpSession& session);
		std::string BuildRecvFailureMessage(const SClientIocpSession& session, int errorCode, bool timedOut) const;
		void EnqueueCommand(EClientCommandType type, int sessionIndex);
		void FailSession(SClientIocpSession& session, const std::string& errorMessage);
		void FinalizeSessionLocked(SClientIocpSession& session, bool succeeded, const std::string& errorMessage);

	private:
		const SClientOptions& m_options;
		FRttMetricsRuntime* m_rttMetricsRuntime = nullptr;
		HANDLE m_iocpHandle = nullptr;
		std::vector<std::unique_ptr<SClientIocpSession>> m_sessions;
		std::vector<std::thread> m_workerThreads;
		std::thread m_schedulerThread;
		std::mutex m_commandMutex;
		std::deque<SClientCommand> m_commands;
		std::mutex m_completionMutex;
		std::condition_variable m_completionCondition;
		std::atomic<int> m_completedSessionCount = 0;
		std::atomic<bool> m_stopRequested = false;
		std::atomic<bool> m_abortRequested = false;
		std::string m_runtimeError;
	};

	SClientIocpSession::SClientIocpSession()
		: packetCipher(
			  []()
			  {
				  NetworkLib::Crypto::SDefaultPacketCipherConfig cipherConfig{};
				  cipherConfig.packetKey = kPacketKey;
				  return NetworkLib::Crypto::FDefaultPacketCipher(cipherConfig);
			  }())
	{
		recvContext.operation = EClientIoOperation::Recv;
		recvContext.session = this;
		sendContext.operation = EClientIoOperation::Send;
		sendContext.session = this;
	}

	FIocpEchoClientRuntime::FIocpEchoClientRuntime(
		const SClientOptions& options,
		FRttMetricsRuntime* const rttMetricsRuntime)
		: m_options(options)
		, m_rttMetricsRuntime(rttMetricsRuntime)
	{
	}

	bool FIocpEchoClientRuntime::Run(
		std::vector<SSessionResult>& outSessionResults)
	{
		m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
		if (m_iocpHandle == nullptr)
		{
			m_runtimeError = "CreateIoCompletionPort failed.";
			return false;
		}

		m_sessions.reserve(static_cast<std::size_t>(m_options.sessionCount));
		for (int sessionIndex = 0; sessionIndex < m_options.sessionCount; ++sessionIndex)
		{
			auto session = std::make_unique<SClientIocpSession>();
			session->sessionIndex = sessionIndex;
			session->randomEngine.seed(
				static_cast<std::uint32_t>(GetTickCount64()) ^ static_cast<std::uint32_t>(sessionIndex * 2654435761u));
			session->recvContext.buffer.resize(static_cast<std::size_t>(std::max(1, m_options.recvBufferSize)));
			session->inboundBuffer.reserve(static_cast<std::size_t>(std::max(1, m_options.recvBufferSize) * 2));
			m_sessions.push_back(std::move(session));
		}

		StartWorkers();
		m_schedulerThread = std::thread(
			[this]()
			{
				SchedulerLoop();
			});

		if (m_options.connectsPerSecond <= 0)
		{
			for (const auto& session : m_sessions)
			{
				std::string errorMessage;
				if (!ConnectSession(*session, errorMessage))
				{
					FailSession(*session, errorMessage);
					break;
				}
			}
		}
		else
		{
			const auto connectScheduleBaseTime = std::chrono::steady_clock::now();
			for (std::size_t sessionIndex = 0; sessionIndex < m_sessions.size(); ++sessionIndex)
			{
				SClientIocpSession& session = *m_sessions[sessionIndex];
				std::lock_guard<std::mutex> lock(session.mutex);
				session.state = EClientSessionState::WaitingConnect;
				session.wakeTime =
					connectScheduleBaseTime + std::chrono::microseconds(static_cast<long long>(
												  (1000000ll * static_cast<long long>(sessionIndex)) / m_options.connectsPerSecond));
			}
		}

		WaitForCompletion();
		RequestStop();
		JoinThreads();

		outSessionResults.clear();
		outSessionResults.reserve(m_sessions.size());
		for (const auto& session : m_sessions)
		{
			std::lock_guard<std::mutex> lock(session->mutex);
			outSessionResults.push_back(session->result);
		}

		return m_runtimeError.empty();
	}

	void FIocpEchoClientRuntime::StartWorkers()
	{
		const int workerThreadCount = std::max(1, m_options.workerThreadCount);
		m_workerThreads.reserve(static_cast<std::size_t>(workerThreadCount));
		for (int workerIndex = 0; workerIndex < workerThreadCount; ++workerIndex)
		{
			m_workerThreads.emplace_back(
				[this]()
				{
					FRttThreadLocalCollector rttCollector(m_rttMetricsRuntime);
					WorkerLoop(rttCollector);
				});
		}
	}

	void FIocpEchoClientRuntime::WaitForCompletion()
	{
		std::unique_lock<std::mutex> lock(m_completionMutex);
		m_completionCondition.wait(lock,
			[this]()
			{
				return m_completedSessionCount.load() >= m_options.sessionCount;
			});
	}

	void FIocpEchoClientRuntime::RequestStop()
	{
		m_stopRequested.store(true);
		if (m_iocpHandle != nullptr)
		{
			for (std::size_t index = 0; index < m_workerThreads.size(); ++index)
			{
				PostQueuedCompletionStatus(m_iocpHandle, 0, kShutdownCompletionKey, nullptr);
			}
		}
	}

	void FIocpEchoClientRuntime::JoinThreads()
	{
		if (m_schedulerThread.joinable())
		{
			m_schedulerThread.join();
		}

		for (std::thread& workerThread : m_workerThreads)
		{
			if (workerThread.joinable())
			{
				workerThread.join();
			}
		}

		if (m_iocpHandle != nullptr)
		{
			CloseHandle(m_iocpHandle);
			m_iocpHandle = nullptr;
		}
	}

	bool FIocpEchoClientRuntime::ConnectSession(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		SOCKET connectedSocket = INVALID_SOCKET;
		if (!TryConnectSocketOverlapped(m_options, connectedSocket, outErrorMessage))
		{
			return false;
		}

		if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(connectedSocket), m_iocpHandle, 0, 0) == nullptr)
		{
			outErrorMessage = "CreateIoCompletionPort attach failed.";
			closesocket(connectedSocket);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(session.mutex);
			if (session.finalized)
			{
				closesocket(connectedSocket);
				return false;
			}

			if (session.socketHandle != INVALID_SOCKET)
			{
				closesocket(session.socketHandle);
			}

			session.socketHandle = connectedSocket;
			session.currentRoomId.reset();
			session.roomEnterAttemptCount = 0;
			session.roomChangeAttemptCount = 0;
			session.expectedResponseCount = 0;
			session.cycleReceivedResponseCount = 0;
			session.expectedResponseCounts.clear();
			session.expectedResponseMetrics.clear();
			session.inboundBuffer.clear();
			session.sendQueue.clear();
			session.activeSendBuffer.clear();
			session.activeSendOffset = 0;
			session.sendInFlight = false;
			session.recvPosted = false;
			ClearWaitStateLocked(session);
			if (!session.holdDeadlineStarted)
			{
				session.deadline =
					std::chrono::steady_clock::now() + std::chrono::seconds(m_options.holdSeconds > 0 ? m_options.holdSeconds : 0);
				session.holdDeadlineStarted = true;
			}

			if (!PostRecvLocked(session, outErrorMessage))
			{
				closesocket(session.socketHandle);
				session.socketHandle = INVALID_SOCKET;
				return false;
			}

			if (!SendLoginLocked(session, outErrorMessage))
			{
				closesocket(session.socketHandle);
				session.socketHandle = INVALID_SOCKET;
				return false;
			}
		}

		return true;
	}

	void FIocpEchoClientRuntime::SchedulerLoop()
	{
		FRttThreadLocalCollector rttCollector(m_rttMetricsRuntime);
		while (!m_stopRequested.load())
		{
			const auto now = std::chrono::steady_clock::now();
			for (const auto& session : m_sessions)
			{
				std::lock_guard<std::mutex> lock(session->mutex);
				if (session->finalized)
				{
					continue;
				}

				if (m_abortRequested.load())
				{
					FinalizeSessionLocked(*session, false, "aborted due to peer failure.");
					continue;
				}

				if (session->timeoutStage.has_value() && session->timeoutDeadline != std::chrono::steady_clock::time_point::max() &&
					now >= session->timeoutDeadline)
				{
					rttCollector.RecordTimeout(ToRttStageIndex(session->timeoutStage.value()), std::chrono::system_clock::now());
					FinalizeSessionLocked(*session, false, BuildRecvFailureMessage(*session, WSAETIMEDOUT, true));
					continue;
				}

				if (!session->commandPending &&
					(session->state == EClientSessionState::WaitingConnect || session->state == EClientSessionState::WaitingInterval ||
						session->state == EClientSessionState::WaitingReconnect) &&
					now >= session->wakeTime)
				{
					session->commandPending = true;
					EnqueueCommand(session->state == EClientSessionState::WaitingConnect
									   ? EClientCommandType::Connect
									   : (session->state == EClientSessionState::WaitingReconnect ? EClientCommandType::Reconnect
																								  : EClientCommandType::ContinueCycle),
						session->sessionIndex);
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}

	void FIocpEchoClientRuntime::WorkerLoop(
		FRttThreadLocalCollector& rttCollector)
	{
		while (true)
		{
			DWORD transferredBytes = 0;
			ULONG_PTR completionKey = 0;
			LPOVERLAPPED overlapped = nullptr;
			const BOOL completionResult = GetQueuedCompletionStatus(m_iocpHandle, &transferredBytes, &completionKey, &overlapped, INFINITE);

			if (completionKey == kShutdownCompletionKey && overlapped == nullptr)
			{
				return;
			}

			if (completionKey == kCommandCompletionKey && overlapped == nullptr)
			{
				DrainCommands(rttCollector);
				continue;
			}

			if (overlapped == nullptr)
			{
				continue;
			}

			auto* ioContext = reinterpret_cast<SClientIoContext*>(overlapped);
			if (ioContext->session == nullptr)
			{
				continue;
			}

			if (!completionResult)
			{
				HandleIoFailure(*ioContext->session, ioContext->operation, GetLastError(), rttCollector);
				continue;
			}

			switch (ioContext->operation)
			{
				case EClientIoOperation::Recv:
					HandleRecvCompletion(*ioContext->session, transferredBytes, rttCollector);
					break;

				case EClientIoOperation::Send:
					HandleSendCompletion(*ioContext->session, transferredBytes);
					break;
			}
		}
	}

	void FIocpEchoClientRuntime::DrainCommands(
		FRttThreadLocalCollector& rttCollector)
	{
		while (true)
		{
			SClientCommand command{};
			{
				std::lock_guard<std::mutex> lock(m_commandMutex);
				if (m_commands.empty())
				{
					return;
				}

				command = m_commands.front();
				m_commands.pop_front();
			}

			if (command.sessionIndex < 0 || command.sessionIndex >= static_cast<int>(m_sessions.size()))
			{
				continue;
			}

			SClientIocpSession& session = *m_sessions[static_cast<std::size_t>(command.sessionIndex)];
			switch (command.type)
			{
				case EClientCommandType::Connect:
					HandleConnectCommand(session);
					break;

				case EClientCommandType::ContinueCycle:
					HandleContinueCommand(session);
					break;

				case EClientCommandType::Reconnect:
					HandleReconnectCommand(session);
					break;
			}
		}
	}

	void FIocpEchoClientRuntime::HandleConnectCommand(
		SClientIocpSession& session)
	{
		{
			std::lock_guard<std::mutex> lock(session.mutex);
			session.commandPending = false;
			if (session.finalized || session.state != EClientSessionState::WaitingConnect)
			{
				return;
			}
		}

		std::string errorMessage;
		if (!ConnectSession(session, errorMessage))
		{
			FailSession(session, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleContinueCommand(
		SClientIocpSession& session)
	{
		std::string errorMessage;
		std::lock_guard<std::mutex> lock(session.mutex);
		session.commandPending = false;
		if (session.finalized || session.state != EClientSessionState::WaitingInterval)
		{
			return;
		}

		if (!StartEchoCycleLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleReconnectCommand(
		SClientIocpSession& session)
	{
		{
			std::lock_guard<std::mutex> lock(session.mutex);
			session.commandPending = false;
			if (session.finalized || session.state != EClientSessionState::WaitingReconnect)
			{
				return;
			}
		}

		std::string errorMessage;
		if (!ConnectSession(session, errorMessage))
		{
			FailSession(session, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleIoFailure(
		SClientIocpSession& session,
		const EClientIoOperation operation,
		const int errorCode,
		FRttThreadLocalCollector& rttCollector)
	{
		std::lock_guard<std::mutex> lock(session.mutex);
		if (session.finalized)
		{
			return;
		}

		if (operation == EClientIoOperation::Recv)
		{
			if (errorCode == WSAETIMEDOUT && session.timeoutStage.has_value())
			{
				rttCollector.RecordTimeout(ToRttStageIndex(session.timeoutStage.value()), std::chrono::system_clock::now());
			}

			session.recvPosted = false;
			FinalizeSessionLocked(session, false, BuildRecvFailureMessage(session, errorCode, errorCode == WSAETIMEDOUT));
			return;
		}

		session.sendInFlight = false;
		std::ostringstream oss;
		oss << "send failed. sessionIndex=" << session.sessionIndex << " error=" << errorCode;
		FinalizeSessionLocked(session, false, oss.str());
	}

	void FIocpEchoClientRuntime::HandleRecvCompletion(
		SClientIocpSession& session,
		const DWORD transferredBytes,
		FRttThreadLocalCollector& rttCollector)
	{
		std::lock_guard<std::mutex> lock(session.mutex);
		session.recvPosted = false;
		if (session.finalized)
		{
			return;
		}

		if (transferredBytes == 0)
		{
			FinalizeSessionLocked(session, false, BuildRecvFailureMessage(session, 0, false));
			return;
		}

		session.inboundBuffer.insert(session.inboundBuffer.end(),
			session.recvContext.buffer.begin(),
			session.recvContext.buffer.begin() + static_cast<std::ptrdiff_t>(transferredBytes));

		NetworkLib::Packet::Framing::SFramedPacket framedPacket{};
		NetworkLib::Packet::View::FPacketView contentPacketView{};
		while (!session.finalized && session.packetFramer.TryExtractPacket(session.inboundBuffer, framedPacket))
		{
			const std::uint8_t responseChecksum = NetworkLib::Packet::Framing::CalculatePacketChecksum(
				framedPacket.payload.data(), static_cast<std::int32_t>(framedPacket.payload.size()));
			if (responseChecksum != framedPacket.checkSum)
			{
				FinalizeSessionLocked(session, false, "packet checksum failed.");
				break;
			}

			session.packetCipher.Decode(framedPacket.payload.data(), static_cast<int>(framedPacket.payload.size()), framedPacket.randomKey);

			NetworkLib::Packet::View::FPacketView transportPacketView{};
			transportPacketView.randomKey = framedPacket.randomKey;
			transportPacketView.checkSum = framedPacket.checkSum;
			transportPacketView.payload = framedPacket.payload.data();
			transportPacketView.payloadLength = static_cast<std::int32_t>(framedPacket.payload.size());

			if (!NetworkLib::Packet::Serialization::TryParseContentPacketView(transportPacketView, contentPacketView))
			{
				FinalizeSessionLocked(session, false, "response content header parse failed.");
				break;
			}

			if (session.singlePendingRequest.has_value())
			{
				rttCollector.RecordSample(session.singlePendingRequest.value(), std::chrono::system_clock::now());
				session.singlePendingRequest.reset();
			}

			HandleContentPacketLocked(session, contentPacketView, rttCollector);
		}

		if (!session.finalized && session.packetFramer.HasInvalidPacketHeader(session.inboundBuffer))
		{
			FinalizeSessionLocked(session, false, "oversized response packet header rejected.");
		}

		if (!session.finalized)
		{
			std::string errorMessage;
			if (!PostRecvLocked(session, errorMessage))
			{
				FinalizeSessionLocked(session, false, errorMessage);
			}
		}
	}

	void FIocpEchoClientRuntime::HandleSendCompletion(
		SClientIocpSession& session,
		const DWORD transferredBytes)
	{
		std::lock_guard<std::mutex> lock(session.mutex);
		if (session.finalized)
		{
			return;
		}

		session.activeSendOffset += static_cast<std::size_t>(transferredBytes);
		if (session.activeSendOffset < session.activeSendBuffer.size())
		{
			std::string errorMessage;
			if (!SubmitActiveSendLocked(session, errorMessage))
			{
				FinalizeSessionLocked(session, false, errorMessage);
			}
			return;
		}

		session.activeSendBuffer.clear();
		session.activeSendOffset = 0;
		session.sendInFlight = false;

		std::string errorMessage;
		if (!StartNextSendLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleContentPacketLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& contentPacketView,
		FRttThreadLocalCollector& rttCollector)
	{
		switch (session.state)
		{
			case EClientSessionState::WaitingLoginResponse:
				HandleLoginResponseLocked(session, contentPacketView);
				break;

			case EClientSessionState::WaitingRoomListForEnter:
				HandleRoomListForEnterLocked(session, contentPacketView);
				break;

			case EClientSessionState::WaitingRoomEnterResponse:
				HandleRoomEnterResponseLocked(session, contentPacketView);
				break;

			case EClientSessionState::WaitingEchoResponses:
				HandleEchoResponseLocked(session, contentPacketView, rttCollector);
				break;

			case EClientSessionState::WaitingRoomChangeList:
				HandleRoomChangeListLocked(session, contentPacketView);
				break;

			case EClientSessionState::WaitingRoomChangeResponse:
				HandleRoomChangeResponseLocked(session, contentPacketView);
				break;

			default:
				FinalizeSessionLocked(session, false, "unexpected packet for current session state.");
				break;
		}
	}

	void FIocpEchoClientRuntime::HandleLoginResponseLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView)
	{
		if (packetView.opcode != Generated::Login::FLoginRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected login response opcode: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Login::FLoginRp loginResponse;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, loginResponse))
		{
			FinalizeSessionLocked(session, false, "login response deserialize failed.");
			return;
		}

		const std::uint32_t expectedUserId = m_options.loginUserIdBase + static_cast<std::uint32_t>(session.sessionIndex);
		if (!loginResponse.success || loginResponse.userId != expectedUserId)
		{
			FinalizeSessionLocked(session, false, "login validation failed.");
			return;
		}

		if (m_options.bootstrapTrace)
		{
			const std::lock_guard<std::mutex> outputLock(g_consoleOutputMutex);
			std::cerr << "bootstrap trace: login response ok. sessionIndex=" << session.sessionIndex << " userId=" << loginResponse.userId
					  << "\n";
		}

		session.roomEnterAttemptCount = 0;
		std::string errorMessage;
		if (!SendRoomListForEnterLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleRoomListForEnterLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView)
	{
		if (packetView.opcode != Generated::Chat::FRoomListRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected room list response opcode: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Chat::FRoomListRp roomListResponse;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, roomListResponse))
		{
			FinalizeSessionLocked(session, false, "room list response deserialize failed.");
			return;
		}

		std::vector<SRoomCandidate> roomCandidates;
		if (!TryBuildRoomCandidates(roomListResponse, roomCandidates))
		{
			FinalizeSessionLocked(session, false, "room list response validation failed.");
			return;
		}

		const auto joinableRooms = BuildJoinableRoomCandidates(roomCandidates);
		const auto targetRoomCandidate = PickRandomRoomCandidate(joinableRooms, session.randomEngine);
		if (!targetRoomCandidate.has_value())
		{
			FinalizeSessionLocked(session, false, "no joinable room available.");
			return;
		}

		std::string errorMessage;
		if (!SendRoomEnterLocked(session, targetRoomCandidate->roomId, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleRoomEnterResponseLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView)
	{
		if (packetView.opcode != Generated::Chat::FRoomEnterRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected room enter response opcode: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Chat::FRoomEnterRp roomEnterResponse;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, roomEnterResponse))
		{
			FinalizeSessionLocked(session, false, "room enter response deserialize failed.");
			return;
		}

		if (roomEnterResponse.success)
		{
			session.currentRoomId = roomEnterResponse.roomId;
			TraceSession(m_options, session.sessionIndex, "recv RoomEnterRp success roomId=" + std::to_string(roomEnterResponse.roomId));
			if (m_options.bootstrapTrace)
			{
				const std::lock_guard<std::mutex> outputLock(g_consoleOutputMutex);
				std::cerr << "bootstrap trace: room enter ok. sessionIndex=" << session.sessionIndex
						  << " roomId=" << roomEnterResponse.roomId << "\n";
			}

			std::string errorMessage;
			if (!StartEchoCycleLocked(session, errorMessage))
			{
				FinalizeSessionLocked(session, false, errorMessage);
			}
			return;
		}

		const auto resultCode = static_cast<EchoServer::Contents::ERoomFlowResultCode>(roomEnterResponse.resultCode);
		if (!EchoServer::Contents::IsNormalRoomFlowFailure(resultCode))
		{
			std::ostringstream oss;
			oss << "room enter failed with abnormal resultCode=" << EchoServer::Contents::ToString(resultCode);
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		++session.roomEnterAttemptCount;
		if (session.roomEnterAttemptCount >= m_options.maxRoomEnterRetryCount)
		{
			FinalizeSessionLocked(session, false, "room enter retry exhausted.");
			return;
		}

		std::string errorMessage;
		if (!SendRoomListForEnterLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleEchoResponseLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView,
		FRttThreadLocalCollector& rttCollector)
	{
		if (packetView.opcode != Generated::Echo::FEchoRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected opcode: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Echo::FEchoRp responsePacket;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, responsePacket))
		{
			FinalizeSessionLocked(session, false, "response packet deserialize failed.");
			return;
		}

		if (!responsePacket.ContainsBorrowedViews())
		{
			FinalizeSessionLocked(session, false, "echo response should report borrowed view payload.");
			return;
		}

		const std::string responseMessage(responsePacket.GetMessageValue());
		TraceSession(m_options,
			session.sessionIndex,
			"recv EchoRp message=" + responseMessage +
				" currentRoomId=" + (session.currentRoomId.has_value() ? std::to_string(*session.currentRoomId) : std::string("none")));

		auto expectedIt = session.expectedResponseCounts.find(responseMessage);
		if (expectedIt == session.expectedResponseCounts.end() || expectedIt->second <= 0)
		{
			std::ostringstream oss;
			oss << "unexpected response=" << responseMessage;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		--expectedIt->second;

		auto pendingMetricIt = session.expectedResponseMetrics.find(responseMessage);
		if (pendingMetricIt != session.expectedResponseMetrics.end())
		{
			rttCollector.RecordSample(pendingMetricIt->second, std::chrono::system_clock::now());
			session.expectedResponseMetrics.erase(pendingMetricIt);
		}

		if (m_options.verbose)
		{
			const std::lock_guard<std::mutex> outputLock(g_consoleOutputMutex);
			std::cout << "session[" << session.sessionIndex << "] response[" << session.result.receivedResponseCount
					  << "]: " << responseMessage << "\n";
		}

		++session.cycleReceivedResponseCount;
		++session.result.receivedResponseCount;
		RefreshWaitDeadlineLocked(session);

		if (session.cycleReceivedResponseCount < session.expectedResponseCount)
		{
			return;
		}

		for (const auto& [message, remainingCount] : session.expectedResponseCounts)
		{
			if (remainingCount != 0)
			{
				std::ostringstream oss;
				oss << "missing response=" << message << " remaining=" << remainingCount;
				FinalizeSessionLocked(session, false, oss.str());
				return;
			}
		}

		if (session.currentRoomId.has_value() && ShouldAttemptRoomChange(m_options, session.randomEngine))
		{
			session.roomChangeAttemptCount = 0;
			std::string errorMessage;
			if (!SendRoomChangeListLocked(session, errorMessage))
			{
				FinalizeSessionLocked(session, false, errorMessage);
			}
			return;
		}

		OnCycleCompletedLocked(session);
	}

	void FIocpEchoClientRuntime::HandleRoomChangeListLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView)
	{
		if (packetView.opcode != Generated::Chat::FRoomListRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected room list response opcode during change: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Chat::FRoomListRp roomListResponse;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, roomListResponse))
		{
			FinalizeSessionLocked(session, false, "room list response deserialize failed during change.");
			return;
		}

		std::vector<SRoomCandidate> roomCandidates;
		if (!TryBuildRoomCandidates(roomListResponse, roomCandidates))
		{
			FinalizeSessionLocked(session, false, "room list response validation failed during change.");
			return;
		}

		const auto joinableRooms = BuildJoinableRoomCandidates(roomCandidates, session.currentRoomId);
		const auto targetRoomCandidate = PickRandomRoomCandidate(joinableRooms, session.randomEngine);
		if (!targetRoomCandidate.has_value())
		{
			OnCycleCompletedLocked(session);
			return;
		}

		std::string errorMessage;
		if (!SendRoomChangeLocked(session, targetRoomCandidate->roomId, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::HandleRoomChangeResponseLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::View::FPacketView& packetView)
	{
		if (packetView.opcode != Generated::Chat::FRoomChangeRp::kOpcode)
		{
			std::ostringstream oss;
			oss << "unexpected room change response opcode: " << packetView.opcode;
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		Generated::Chat::FRoomChangeRp roomChangeResponse;
		if (!NetworkLib::Packet::Serialization::DeserializeContentPacket(packetView, roomChangeResponse))
		{
			FinalizeSessionLocked(session, false, "room change response deserialize failed.");
			return;
		}

		if (roomChangeResponse.success)
		{
			TraceSession(m_options,
				session.sessionIndex,
				"recv RoomChangeRp success previousRoomId=" + std::to_string(roomChangeResponse.previousRoomId) +
					" currentRoomId=" + std::to_string(roomChangeResponse.currentRoomId));
			session.currentRoomId = roomChangeResponse.currentRoomId;
			OnCycleCompletedLocked(session);
			return;
		}

		const auto resultCode = static_cast<EchoServer::Contents::ERoomFlowResultCode>(roomChangeResponse.resultCode);
		TraceSession(m_options,
			session.sessionIndex,
			"recv RoomChangeRp failure resultCode=" + std::string(EchoServer::Contents::ToString(resultCode)));
		if (!EchoServer::Contents::IsNormalRoomFlowFailure(resultCode))
		{
			std::ostringstream oss;
			oss << "room change failed with abnormal resultCode=" << EchoServer::Contents::ToString(resultCode);
			FinalizeSessionLocked(session, false, oss.str());
			return;
		}

		++session.roomChangeAttemptCount;
		if (session.roomChangeAttemptCount >= m_options.maxRoomChangeRetryCount)
		{
			OnCycleCompletedLocked(session);
			return;
		}

		std::string errorMessage;
		if (!SendRoomChangeListLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	void FIocpEchoClientRuntime::OnCycleCompletedLocked(
		SClientIocpSession& session)
	{
		session.expectedResponseCounts.clear();
		session.expectedResponseMetrics.clear();
		session.expectedResponseCount = 0;
		session.cycleReceivedResponseCount = 0;
		ClearWaitStateLocked(session);

		if (m_options.holdSeconds <= 0 || std::chrono::steady_clock::now() >= session.deadline)
		{
			FinalizeSessionLocked(session, true, {});
			return;
		}

		if (ShouldReconnect(m_options, session.randomEngine))
		{
			if (session.socketHandle != INVALID_SOCKET)
			{
				shutdown(session.socketHandle, SD_BOTH);
				closesocket(session.socketHandle);
				session.socketHandle = INVALID_SOCKET;
			}

			session.currentRoomId.reset();
			session.state = EClientSessionState::WaitingReconnect;
			session.wakeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, m_options.reconnectDelayMs));
			if (m_options.reconnectDelayMs <= 0)
			{
				session.commandPending = true;
				EnqueueCommand(EClientCommandType::Reconnect, session.sessionIndex);
			}
			return;
		}

		if (m_options.intervalMs > 0)
		{
			session.state = EClientSessionState::WaitingInterval;
			session.wakeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_options.intervalMs);
			return;
		}

		std::string errorMessage;
		if (!StartEchoCycleLocked(session, errorMessage))
		{
			FinalizeSessionLocked(session, false, errorMessage);
		}
	}

	bool FIocpEchoClientRuntime::SendLoginLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		Generated::Login::FLoginRq loginRequest;
		loginRequest.userId = m_options.loginUserIdBase + static_cast<std::uint32_t>(session.sessionIndex);
		if (!QueuePacketLocked(session, loginRequest, static_cast<std::uint8_t>((0x21 + session.sessionIndex) & 0xFF), outErrorMessage))
		{
			return false;
		}

		SetWaitStateLocked(session,
			EClientSessionState::WaitingLoginResponse,
			ERttStage::LoginResponse,
			"login-response",
			MakePendingRequest(ERttStage::LoginResponse, session.sessionIndex));
		return true;
	}

	bool FIocpEchoClientRuntime::SendRoomListForEnterLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		Generated::Chat::FRoomListRq roomListRequest;
		if (!QueuePacketLocked(session,
				roomListRequest,
				static_cast<std::uint8_t>((0x41 + session.sessionIndex + session.roomEnterAttemptCount) & 0xFF),
				outErrorMessage))
		{
			return false;
		}

		SetWaitStateLocked(session,
			EClientSessionState::WaitingRoomListForEnter,
			ERttStage::RoomList,
			"room-list",
			MakePendingRequest(ERttStage::RoomList, session.sessionIndex));
		return true;
	}

	bool FIocpEchoClientRuntime::SendRoomEnterLocked(
		SClientIocpSession& session,
		const std::uint32_t roomId,
		std::string& outErrorMessage)
	{
		Generated::Chat::FRoomEnterRq roomEnterRequest;
		roomEnterRequest.roomId = roomId;
		TraceSession(m_options, session.sessionIndex, "send RoomEnterRq roomId=" + std::to_string(roomId));
		if (!QueuePacketLocked(session,
				roomEnterRequest,
				static_cast<std::uint8_t>((0x51 + session.sessionIndex + session.roomEnterAttemptCount) & 0xFF),
				outErrorMessage))
		{
			return false;
		}

		SetWaitStateLocked(session,
			EClientSessionState::WaitingRoomEnterResponse,
			ERttStage::RoomEnter,
			"room-enter",
			MakePendingRequest(ERttStage::RoomEnter, session.sessionIndex));
		return true;
	}

	bool FIocpEchoClientRuntime::SendRoomChangeListLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		Generated::Chat::FRoomListRq roomListRequest;
		if (!QueuePacketLocked(session,
				roomListRequest,
				static_cast<std::uint8_t>((0x71 + session.sessionIndex + session.roomChangeAttemptCount) & 0xFF),
				outErrorMessage))
		{
			return false;
		}

		SetWaitStateLocked(session,
			EClientSessionState::WaitingRoomChangeList,
			ERttStage::RoomChangeList,
			"room-change-list",
			MakePendingRequest(ERttStage::RoomChangeList, session.sessionIndex));
		return true;
	}

	bool FIocpEchoClientRuntime::SendRoomChangeLocked(
		SClientIocpSession& session,
		const std::uint32_t targetRoomId,
		std::string& outErrorMessage)
	{
		Generated::Chat::FRoomChangeRq roomChangeRequest;
		roomChangeRequest.targetRoomId = targetRoomId;
		TraceSession(m_options,
			session.sessionIndex,
			"send RoomChangeRq fromRoomId=" +
				(session.currentRoomId.has_value() ? std::to_string(*session.currentRoomId) : std::string("none")) +
				" targetRoomId=" + std::to_string(targetRoomId));
		if (!QueuePacketLocked(session,
				roomChangeRequest,
				static_cast<std::uint8_t>((0x81 + session.sessionIndex + session.roomChangeAttemptCount) & 0xFF),
				outErrorMessage))
		{
			return false;
		}

		SetWaitStateLocked(session,
			EClientSessionState::WaitingRoomChangeResponse,
			ERttStage::RoomChange,
			"room-change",
			MakePendingRequest(ERttStage::RoomChange, session.sessionIndex));
		return true;
	}

	bool FIocpEchoClientRuntime::StartEchoCycleLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		session.expectedResponseCount = m_options.requestCount * m_options.responseThreadCount * m_options.responsesPerThread;
		session.cycleReceivedResponseCount = 0;
		session.expectedResponseCounts.clear();
		session.expectedResponseMetrics.clear();

		if (session.expectedResponseCount <= 0)
		{
			outErrorMessage = "invalid expected response count.";
			return false;
		}

		TraceSession(m_options,
			session.sessionIndex,
			"send EchoRq batch requestCount=" + std::to_string(m_options.requestCount) +
				" currentRoomId=" + (session.currentRoomId.has_value() ? std::to_string(*session.currentRoomId) : std::string("none")));

		std::vector<char> batchBuffer;
		std::vector<std::string> pendingBatchResponseMetrics;
		for (int requestIndex = 0; requestIndex < m_options.requestCount; ++requestIndex)
		{
			const std::string requestMessage = BuildRequestMessage(session.sessionIndex, session.requestSequence++, m_options.payloadSize);
			const std::vector<std::string> expectedResponses = BuildExpectedResponseMessages(requestMessage, m_options);
			for (const std::string& expectedResponse : expectedResponses)
			{
				++session.expectedResponseCounts[expectedResponse];
				pendingBatchResponseMetrics.push_back(expectedResponse);
			}

			Generated::Echo::FEchoRq requestPacket;
			requestPacket.SetMessageValue(requestMessage);

			const std::uint8_t requestRandomKey = static_cast<std::uint8_t>((0x61 + session.requestSequence + session.sessionIndex) & 0xFF);
			std::vector<char> outboundPacket =
				BuildContentPacketBuffer(session.packetCipher, session.packetFramer, requestPacket, requestRandomKey);
			if (outboundPacket.empty())
			{
				outErrorMessage = "BuildPacket failed.";
				return false;
			}

			batchBuffer.insert(batchBuffer.end(), outboundPacket.begin(), outboundPacket.end());
			const bool shouldFlush = ((requestIndex + 1) % m_options.packetsPerSend) == 0 || requestIndex == m_options.requestCount - 1;
			if (!shouldFlush)
			{
				continue;
			}

			if (!EnqueueSendBufferLocked(session, std::move(batchBuffer), outErrorMessage))
			{
				return false;
			}

			const auto batchSentSteady = std::chrono::steady_clock::now();
			const auto batchSentSystem = std::chrono::system_clock::now();
			for (const std::string& expectedResponse : pendingBatchResponseMetrics)
			{
				SRttPendingRequest pendingRequest{};
				pendingRequest.stageIndex = ToRttStageIndex(ERttStage::EchoResponse);
				pendingRequest.sessionIndex = session.sessionIndex;
				pendingRequest.sentSteady = batchSentSteady;
				pendingRequest.sentSystem = batchSentSystem;
				session.expectedResponseMetrics.insert_or_assign(expectedResponse, pendingRequest);
			}

			batchBuffer.clear();
			pendingBatchResponseMetrics.clear();
		}

		SetWaitStateLocked(session, EClientSessionState::WaitingEchoResponses, ERttStage::EchoResponse, "echo-response", std::nullopt);
		return true;
	}

	bool FIocpEchoClientRuntime::QueuePacketLocked(
		SClientIocpSession& session,
		const NetworkLib::Packet::Serialization::IContentPacket& packet,
		const std::uint8_t randomKey,
		std::string& outErrorMessage)
	{
		std::vector<char> outboundPacket = BuildContentPacketBuffer(session.packetCipher, session.packetFramer, packet, randomKey);
		if (outboundPacket.empty())
		{
			outErrorMessage = "BuildPacket failed.";
			return false;
		}

		return EnqueueSendBufferLocked(session, std::move(outboundPacket), outErrorMessage);
	}

	bool FIocpEchoClientRuntime::EnqueueSendBufferLocked(
		SClientIocpSession& session,
		std::vector<char>&& buffer,
		std::string& outErrorMessage)
	{
		if (session.finalized)
		{
			outErrorMessage = "session already finalized.";
			return false;
		}

		if (session.socketHandle == INVALID_SOCKET)
		{
			outErrorMessage = "socket is not connected.";
			return false;
		}

		if (buffer.empty())
		{
			return true;
		}

		const int chunkSize = m_options.sendChunkSize;
		if (chunkSize > 0 && chunkSize < static_cast<int>(buffer.size()))
		{
			std::size_t offset = 0;
			while (offset < buffer.size())
			{
				const std::size_t bytesToCopy = std::min(static_cast<std::size_t>(chunkSize), buffer.size() - offset);
				std::vector<char> chunk(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
					buffer.begin() + static_cast<std::ptrdiff_t>(offset + bytesToCopy));
				session.sendQueue.emplace_back(std::move(chunk));
				offset += bytesToCopy;
			}
		}
		else
		{
			session.sendQueue.emplace_back(std::move(buffer));
		}

		return StartNextSendLocked(session, outErrorMessage);
	}

	bool FIocpEchoClientRuntime::StartNextSendLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		if (session.finalized)
		{
			return true;
		}

		if (session.sendInFlight)
		{
			return true;
		}

		if (session.activeSendBuffer.empty())
		{
			if (session.sendQueue.empty())
			{
				return true;
			}

			session.activeSendBuffer = std::move(session.sendQueue.front());
			session.sendQueue.pop_front();
			session.activeSendOffset = 0;
		}

		session.sendInFlight = true;
		if (!SubmitActiveSendLocked(session, outErrorMessage))
		{
			session.sendInFlight = false;
			return false;
		}

		return true;
	}

	bool FIocpEchoClientRuntime::SubmitActiveSendLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		if (session.socketHandle == INVALID_SOCKET)
		{
			outErrorMessage = "socket is not connected.";
			return false;
		}

		if (session.activeSendBuffer.empty() || session.activeSendOffset >= session.activeSendBuffer.size())
		{
			outErrorMessage = "active send buffer is empty.";
			return false;
		}

		std::memset(&session.sendContext.overlapped, 0, sizeof(session.sendContext.overlapped));

		WSABUF sendBuffer{};
		sendBuffer.buf = session.activeSendBuffer.data() + static_cast<std::ptrdiff_t>(session.activeSendOffset);
		sendBuffer.len = static_cast<ULONG>(session.activeSendBuffer.size() - session.activeSendOffset);

		DWORD sentBytes = 0;
		const int sendResult = WSASend(session.socketHandle, &sendBuffer, 1, &sentBytes, 0, &session.sendContext.overlapped, nullptr);
		if (sendResult == SOCKET_ERROR)
		{
			const int errorCode = WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				std::ostringstream oss;
				oss << "WSASend failed. sessionIndex=" << session.sessionIndex << " error=" << errorCode;
				outErrorMessage = oss.str();
				return false;
			}
		}

		return true;
	}

	bool FIocpEchoClientRuntime::PostRecvLocked(
		SClientIocpSession& session,
		std::string& outErrorMessage)
	{
		if (session.finalized)
		{
			return true;
		}

		if (session.recvPosted)
		{
			return true;
		}

		if (session.socketHandle == INVALID_SOCKET)
		{
			outErrorMessage = "socket is not connected.";
			return false;
		}

		if (session.recvContext.buffer.empty())
		{
			session.recvContext.buffer.resize(static_cast<std::size_t>(std::max(1, m_options.recvBufferSize)));
		}

		std::memset(&session.recvContext.overlapped, 0, sizeof(session.recvContext.overlapped));

		WSABUF recvBuffer{};
		recvBuffer.buf = session.recvContext.buffer.data();
		recvBuffer.len = static_cast<ULONG>(session.recvContext.buffer.size());

		DWORD flags = 0;
		DWORD receivedBytes = 0;
		const int recvResult =
			WSARecv(session.socketHandle, &recvBuffer, 1, &receivedBytes, &flags, &session.recvContext.overlapped, nullptr);
		if (recvResult == SOCKET_ERROR)
		{
			const int errorCode = WSAGetLastError();
			if (errorCode != WSA_IO_PENDING)
			{
				std::ostringstream oss;
				oss << "WSARecv failed. sessionIndex=" << session.sessionIndex << " error=" << errorCode;
				outErrorMessage = oss.str();
				return false;
			}
		}

		session.recvPosted = true;
		return true;
	}

	void FIocpEchoClientRuntime::SetWaitStateLocked(
		SClientIocpSession& session,
		const EClientSessionState state,
		const ERttStage stage,
		const char* stageName,
		const std::optional<SRttPendingRequest>& pendingRequest)
	{
		session.state = state;
		session.timeoutStage = stage;
		session.timeoutStageName = stageName != nullptr ? stageName : "";
		session.singlePendingRequest = pendingRequest;
		RefreshWaitDeadlineLocked(session);
		TraceSession(m_options,
			session.sessionIndex,
			"wait state=" + std::string(ToString(state)) +
				" stage=" + (session.timeoutStageName.empty() ? std::string("none") : session.timeoutStageName));
	}

	void FIocpEchoClientRuntime::ClearWaitStateLocked(
		SClientIocpSession& session)
	{
		session.timeoutStage.reset();
		session.singlePendingRequest.reset();
		session.timeoutStageName.clear();
		session.timeoutDeadline = std::chrono::steady_clock::time_point::max();
	}

	void FIocpEchoClientRuntime::RefreshWaitDeadlineLocked(
		SClientIocpSession& session)
	{
		if (!session.timeoutStage.has_value())
		{
			session.timeoutDeadline = std::chrono::steady_clock::time_point::max();
			return;
		}

		const int timeoutMs =
			ResolveStageRecvTimeoutMs(m_options, session.timeoutStageName.empty() ? nullptr : session.timeoutStageName.c_str());
		if (timeoutMs <= 0)
		{
			session.timeoutDeadline = std::chrono::steady_clock::time_point::max();
			return;
		}

		session.timeoutDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	}

	std::string FIocpEchoClientRuntime::BuildRecvFailureMessage(
		const SClientIocpSession& session,
		const int errorCode,
		const bool timedOut) const
	{
		std::ostringstream oss;
		oss << "recv failed at stage=" << (!session.timeoutStageName.empty() ? session.timeoutStageName : "unknown")
			<< " sessionIndex=" << session.sessionIndex << " error=" << errorCode;
		if (timedOut)
		{
			oss << " (timeout)";
		}

		return oss.str();
	}

	void FIocpEchoClientRuntime::EnqueueCommand(
		const EClientCommandType type,
		const int sessionIndex)
	{
		{
			std::lock_guard<std::mutex> lock(m_commandMutex);
			m_commands.push_back({type, sessionIndex});
		}

		if (m_iocpHandle != nullptr)
		{
			PostQueuedCompletionStatus(m_iocpHandle, 0, kCommandCompletionKey, nullptr);
		}
	}

	void FIocpEchoClientRuntime::FailSession(
		SClientIocpSession& session,
		const std::string& errorMessage)
	{
		std::lock_guard<std::mutex> lock(session.mutex);
		FinalizeSessionLocked(session, false, errorMessage);
	}

	void FIocpEchoClientRuntime::FinalizeSessionLocked(
		SClientIocpSession& session,
		const bool succeeded,
		const std::string& errorMessage)
	{
		if (session.finalized)
		{
			return;
		}

		const std::string lastState = ToString(session.state);
		const std::string lastStage = session.timeoutStageName.empty() ? std::string("none") : session.timeoutStageName;
		session.finalized = true;
		session.state = succeeded ? EClientSessionState::Completed : EClientSessionState::Failed;
		session.commandPending = false;
		session.recvPosted = false;
		session.sendInFlight = false;
		session.sendQueue.clear();
		session.activeSendBuffer.clear();
		session.activeSendOffset = 0;
		session.expectedResponseCounts.clear();
		session.expectedResponseMetrics.clear();
		session.expectedResponseCount = 0;
		session.cycleReceivedResponseCount = 0;
		ClearWaitStateLocked(session);

		if (session.socketHandle != INVALID_SOCKET)
		{
			shutdown(session.socketHandle, SD_BOTH);
			closesocket(session.socketHandle);
			session.socketHandle = INVALID_SOCKET;
		}

		session.result.succeeded = succeeded;
		session.result.errorMessage = succeeded ? std::string() : errorMessage;
		TraceSession(m_options,
			session.sessionIndex,
			"finalize result=" + std::string(succeeded ? "success" : "failure") + " lastState=" + lastState + " lastStage=" + lastStage +
				" roomEnterAttempts=" + std::to_string(session.roomEnterAttemptCount) +
				" roomChangeAttempts=" + std::to_string(session.roomChangeAttemptCount) + " receivedResponses=" +
				std::to_string(session.result.receivedResponseCount) + (succeeded ? std::string() : " error=" + errorMessage));

		{
			std::lock_guard<std::mutex> completionLock(m_completionMutex);
			if (!succeeded)
			{
				if (m_runtimeError.empty())
				{
					m_runtimeError = errorMessage;
					std::cerr << "[runtime] first failure. sessionIndex=" << session.sessionIndex << " lastState=" << lastState
							  << " lastStage=" << lastStage << " receivedResponses=" << session.result.receivedResponseCount
							  << " error=" << errorMessage << "\n";
				}

				m_abortRequested.store(true);
			}

			++m_completedSessionCount;
		}

		m_completionCondition.notify_all();
	}
}

int main(
	int argc,
	char* argv[])
{
	SClientOptions options{};
	const std::filesystem::path executableDirectory = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
	Generated::Config::EchoClient::FEchoClientConfigDocument configDocument{};
	std::string configErrorMessage;
	const std::filesystem::path configPath =
		TryGetConfigPathOverride(argc, argv).value_or(ResolveDefaultEchoClientConfigPath(executableDirectory));
	if (!Generated::Config::EchoClient::FEchoClientConfigLoader::LoadFromFile(configPath, configDocument, configErrorMessage))
	{
		std::cerr << "EchoClient config load failed: " << configErrorMessage << "\n";
		return 1;
	}

	ApplyEchoClientConfigDocument(configDocument, executableDirectory, options);

	if (!ParseArguments(argc, argv, options))
	{
		std::cerr << "usage: EchoClient.exe [--config path] [--server-ip 127.0.0.1] [--port 19000] [--login-userid-base 1000] "
				  << "[--sessions 1] [--count 10] [--payload-size 64] [--send-chunk-size 8] [--send-chunk-delay-ms 1] "
				  << "[--recv-buffer-size 16] [--response-thread-count 1] [--responses-per-thread 1] [--worker-thread-count 4] "
					 "[--hold-seconds 0] "
				  << "[--interval-ms 1000] [--packets-per-send 1] [--reconnect-probability-percent 0] [--reconnect-delay-ms 100] "
				  << "[--recv-timeout-ms 0] [--room-list-recv-timeout-ms -1] [--echo-recv-timeout-ms -1] "
				  << "[--rtt-csv-path path] [--rtt-flush-interval-seconds 60] "
				  << "[--room-change-probability-percent 25] [--max-room-enter-retries 5] "
				  << "[--max-room-change-retries 3] [--disable-page-pool] [--page-size 4096] [--bootstrap-trace] [--quiet]\n";
		return 1;
	}

	NetworkLib::Packet::Buffer::FPacketBuffer::ConfigurePageReuse(options.enablePagePool, static_cast<std::size_t>(options.pageSize));

	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "WSAStartup failed.\n";
		return 1;
	}

	std::unique_ptr<FRttMetricsRuntime> rttMetricsRuntime;
	std::unique_ptr<FRttCsvLogger> rttCsvLogger;
	if (!options.rttCsvPath.empty())
	{
		rttMetricsRuntime = std::make_unique<FRttMetricsRuntime>(BuildRttMetricsConfig(options));
		rttCsvLogger = std::make_unique<FRttCsvLogger>(*rttMetricsRuntime, options.rttCsvPath);
		rttCsvLogger->Start();
	}

	std::vector<SSessionResult> sessionResults(static_cast<std::size_t>(options.sessionCount));
	FIocpEchoClientRuntime clientRuntime(options, rttMetricsRuntime.get());
	const bool runSucceeded = clientRuntime.Run(sessionResults);

	if (rttCsvLogger)
	{
		rttCsvLogger->Stop();
	}

	WSACleanup();

	int totalResponses = 0;
	int successCount = 0;
	for (int sessionIndex = 0; sessionIndex < options.sessionCount; ++sessionIndex)
	{
		const SSessionResult& sessionResult = sessionResults[static_cast<std::size_t>(sessionIndex)];
		totalResponses += sessionResult.receivedResponseCount;
		if (!sessionResult.succeeded)
		{
			std::cerr << "session[" << sessionIndex << "] failed: " << sessionResult.errorMessage << "\n";
			return 1;
		}

		++successCount;
	}

	if (!runSucceeded)
	{
		return 1;
	}

	std::cout << "echo validation succeeded. sessions=" << successCount << " responses=" << totalResponses
			  << " payloadSize=" << options.payloadSize << " sendChunkSize=" << options.sendChunkSize
			  << " recvBufferSize=" << options.recvBufferSize << " workerThreadCount=" << options.workerThreadCount
			  << " responseThreadCount=" << options.responseThreadCount << " responsesPerThread=" << options.responsesPerThread
			  << " intervalMs=" << options.intervalMs << " packetsPerSend=" << options.packetsPerSend
			  << " reconnectProbabilityPercent=" << options.reconnectProbabilityPercent
			  << " connectsPerSecond=" << options.connectsPerSecond
			  << " roomChangeProbabilityPercent=" << options.roomChangeProbabilityPercent << " holdSeconds=" << options.holdSeconds << "\n";
	return 0;
}
