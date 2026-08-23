#include "ChattingDummyClientPch.h"

#include "ClientNetworkLib/FClientNetwork.h"
#include "Foundation/Diagnostics/Rtt/FRttCsvLogger.h"
#include "Foundation/Diagnostics/Rtt/FRttMetricsRuntime.h"
#include "Foundation/Diagnostics/Rtt/FRttThreadLocalCollector.h"
#include "Generated/Config/ChattingDummy/ChattingDummyConfig.h"
#include "Generated/Packets/Chatting/ChattingPackets.h"
#include "Generated/Packets/Login/LoginPackets.h"
#include "NetworkLib/Packet/Buffer/FPacketBuffer.h"

#include <limits>
#include <optional>
#include <random>

namespace
{
	using FClientNetwork = ClientNetworkLib::FClientNetwork;
	using FClientNetworkConfig = ClientNetworkLib::FClientNetworkConfig;
	using FClientSessionId = ClientNetworkLib::FClientSessionId;
	using FClientEvent = ClientNetworkLib::FClientEvent;
	using EClientEventType = ClientNetworkLib::EClientEventType;

	using FRttMetricsRuntime = Foundation::Diagnostics::FRttMetricsRuntime;
	using FRttThreadLocalCollector = Foundation::Diagnostics::FRttThreadLocalCollector;
	using FRttCsvLogger = Foundation::Diagnostics::FRttCsvLogger;
	using SRttPendingRequest = Foundation::Diagnostics::SRttPendingRequest;

	enum class ERttStage : std::uint8_t
	{
		LoginResponse = 0,
		RoomList = 1,
		RoomChange = 2,
		ChattingResponse = 3
	};

	enum class ESessionState : std::uint8_t
	{
		Idle,
		WaitingLoginResponse,
		WaitingRoomListResponse,
		WaitingRoomChangeResponse,
		ConnectedIdle,
		WaitingChattingResponse,
		Disconnecting,
		Failed
	};

	struct SOptions
	{
		std::string serverIp = "127.0.0.1";
		std::uint16_t port = 19100;
		std::uint32_t packetKey = 55;
		int workerThreadCount = 4;
		int sessionCount = 100;
		int connectsPerSecond = 50;
		std::uint32_t loginUserIdBase = 100000;
		int runSeconds = 60;
		int sendIntervalMs = 1000;
		int payloadSizeBytes = 1024;
		bool hiMode = false;
		Generated::Config::ChattingDummy::ERoomSelectionMode roomSelectionMode =
			Generated::Config::ChattingDummy::ERoomSelectionMode::Random;
		std::vector<std::uint32_t> hotspotRoomIds;
		int hotspotBiasPercent = 80;
		int roomChangeProbabilityPercent = 0;
		int reconnectProbabilityPercent = 0;
		int reconnectDelayMs = 100;
		int responseTimeoutMs = 5000;
		int consoleSummaryIntervalSeconds = 5;
		int eventPollMaxCount = 256;
		std::string rttCsvPath;
	};

	struct SRoomCandidate
	{
		std::uint32_t roomId = 0;
		std::string roomName;
		std::uint32_t participantCount = 0;
		std::uint32_t capacity = 0;
		bool joinable = false;
	};

	struct SStats
	{
		std::uint64_t connectAttemptCount = 0;
		std::uint64_t connectSuccessCount = 0;
		std::uint64_t connectFailureCount = 0;
		std::uint64_t loginSuccessCount = 0;
		std::uint64_t roomListResponseCount = 0;
		std::uint64_t roomChangeSuccessCount = 0;
		std::uint64_t roomChangeFailureCount = 0;
		std::uint64_t chattingSendCount = 0;
		std::uint64_t chattingSuccessCount = 0;
		std::uint64_t chattingRejectCount = 0;
		std::uint64_t broadcastReceiveCount = 0;
		std::uint64_t sendPayloadBytes = 0;
		std::uint64_t receivePayloadBytes = 0;
		std::uint64_t reconnectCount = 0;
		std::uint64_t unexpectedDisconnectCount = 0;
		std::uint64_t timeoutCount = 0;
		std::uint64_t sessionErrorEventCount = 0;
		std::uint64_t selfBroadcastCount = 0;
		std::uint64_t invalidRoomBroadcastCount = 0;
		std::uint64_t payloadValidationFailureCount = 0;
		std::uint64_t permanentFailureCount = 0;
	};

	struct SSessionSlot
	{
		int sessionIndex = 0;
		std::uint32_t userId = 0;
		FClientSessionId sessionId = 0;
		ESessionState state = ESessionState::Idle;
		std::optional<std::uint32_t> currentRoomId;
		std::vector<SRoomCandidate> roomCandidates;
		std::mt19937 randomEngine;
		std::chrono::steady_clock::time_point nextActionTime = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point waitDeadline = std::chrono::steady_clock::time_point::max();
		std::optional<SRttPendingRequest> pendingRequest;
		std::uint64_t nextClientMessageId = 1;
		std::size_t roundRobinCursor = 0;
		int recoverableErrorCount = 0;
		bool permanentFailure = false;
		bool reconnectRequested = false;
		std::string lastErrorMessage;
	};

	constexpr int kSleepLoopMs = 1;
	constexpr int kDefaultPageSize = 4096;
	constexpr int kMaxRecoverableErrorCount = 8;

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

	std::filesystem::path ResolveDefaultChattingDummyConfigPath(
		const std::filesystem::path& executableDirectory)
	{
		const std::filesystem::path localPath = executableDirectory / "Config" / "Client" / "ChattingDummy.yaml";
		if (std::filesystem::exists(localPath))
		{
			return localPath;
		}

		return executableDirectory.parent_path() / "Config" / "Client" / "ChattingDummy.yaml";
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

	std::vector<std::uint32_t> ParseRoomIdCsv(
		const std::string& text)
	{
		std::vector<std::uint32_t> roomIds;
		std::stringstream textStream(text);
		std::string token;
		while (std::getline(textStream, token, ','))
		{
			token.erase(std::remove_if(token.begin(),
							token.end(),
							[](const unsigned char character)
							{
								return std::isspace(character) != 0;
							}),
				token.end());

			if (token.empty())
			{
				continue;
			}

			char* parseEnd = nullptr;
			const unsigned long parsedValue = std::strtoul(token.c_str(), &parseEnd, 10);
			if (parseEnd == token.c_str() || *parseEnd != '\0')
			{
				continue;
			}

			roomIds.push_back(static_cast<std::uint32_t>(parsedValue));
		}

		std::sort(roomIds.begin(), roomIds.end());
		roomIds.erase(std::unique(roomIds.begin(), roomIds.end()), roomIds.end());
		return roomIds;
	}

	void ApplyChattingDummyConfigDocument(
		const Generated::Config::ChattingDummy::FChattingDummyConfigDocument& configDocument,
		const std::filesystem::path& executableDirectory,
		SOptions& outOptions)
	{
		outOptions.serverIp = configDocument.ChattingDummy.ServerIp;
		outOptions.port = configDocument.ChattingDummy.Port;
		outOptions.packetKey = configDocument.ChattingDummy.PacketKey;
		outOptions.workerThreadCount = std::max(1, configDocument.ChattingDummy.WorkerThreadCount);
		outOptions.sessionCount = std::max(1, configDocument.ChattingDummy.SessionCount);
		outOptions.connectsPerSecond = std::max(0, configDocument.ChattingDummy.ConnectsPerSecond);
		outOptions.loginUserIdBase = configDocument.ChattingDummy.LoginUserIdBase;
		outOptions.runSeconds = std::max(1, configDocument.ChattingDummy.RunSeconds);
		outOptions.sendIntervalMs = std::max(0, configDocument.ChattingDummy.SendIntervalMs);
		outOptions.payloadSizeBytes = std::max(1, configDocument.ChattingDummy.PayloadSizeBytes);
		outOptions.hiMode = configDocument.ChattingDummy.HiMode;
		outOptions.roomSelectionMode = configDocument.ChattingDummy.RoomSelectionMode;
		outOptions.hotspotRoomIds = ParseRoomIdCsv(configDocument.ChattingDummy.HotspotRoomIds);
		outOptions.hotspotBiasPercent = std::clamp(configDocument.ChattingDummy.HotspotBiasPercent, 0, 100);
		outOptions.roomChangeProbabilityPercent = std::clamp(configDocument.ChattingDummy.RoomChangeProbabilityPercent, 0, 100);
		outOptions.reconnectProbabilityPercent = std::clamp(configDocument.ChattingDummy.ReconnectProbabilityPercent, 0, 100);
		outOptions.reconnectDelayMs = std::max(0, configDocument.ChattingDummy.ReconnectDelayMs);
		outOptions.responseTimeoutMs = std::max(1, configDocument.ChattingDummy.ResponseTimeoutMs);
		outOptions.consoleSummaryIntervalSeconds = std::max(1, configDocument.ChattingDummy.ConsoleSummaryIntervalSeconds);
		outOptions.eventPollMaxCount = std::max(0, configDocument.ChattingDummy.EventPollMaxCount);
		const std::filesystem::path resolvedRttPath = ResolveConfiguredPath(executableDirectory, configDocument.ChattingDummy.RttCsvPath);
		outOptions.rttCsvPath = resolvedRttPath.empty() ? std::string() : resolvedRttPath.string();
	}

	Foundation::Diagnostics::FRttStageIndex ToRttStageIndex(
		const ERttStage stage)
	{
		return static_cast<Foundation::Diagnostics::FRttStageIndex>(stage);
	}

	SRttPendingRequest MakePendingRequest(
		const ERttStage stage,
		const int sessionIndex)
	{
		SRttPendingRequest pendingRequest{};
		pendingRequest.stageIndex = ToRttStageIndex(stage);
		pendingRequest.sessionIndex = sessionIndex;
		pendingRequest.sentSteady = std::chrono::steady_clock::now();
		pendingRequest.sentSystem = std::chrono::system_clock::now();
		return pendingRequest;
	}

	Foundation::Diagnostics::SRttMetricsConfig BuildRttMetricsConfig()
	{
		Foundation::Diagnostics::SRttMetricsConfig config{};
		config.flushIntervalSeconds = 60;
		config.stageNames = {"login-response", "room-list", "room-change", "chatting-response"};
		return config;
	}

	std::vector<std::uint8_t> BuildPayloadPattern(
		const int payloadSizeBytes,
		const bool hiMode)
	{
		if (hiMode)
		{
			return {static_cast<std::uint8_t>('h'), static_cast<std::uint8_t>('i')};
		}

		std::vector<std::uint8_t> payload(static_cast<std::size_t>(std::max(1, payloadSizeBytes)));
		for (std::size_t index = 0; index < payload.size(); ++index)
		{
			payload[index] = static_cast<std::uint8_t>((index * 31u + 7u) & 0xFFu);
		}

		return payload;
	}

	bool ShouldAttemptAction(
		const int probabilityPercent,
		std::mt19937& randomEngine)
	{
		if (probabilityPercent <= 0)
		{
			return false;
		}

		std::uniform_int_distribution<int> distribution(1, 100);
		return distribution(randomEngine) <= probabilityPercent;
	}

	bool TryBuildRoomCandidates(
		const Generated::Chatting::FRoomListRp& roomListResponse,
		std::vector<SRoomCandidate>& outCandidates)
	{
		const std::size_t roomCount = roomListResponse.roomIds.size();
		if (roomListResponse.roomNames.size() != roomCount || roomListResponse.participantCounts.size() != roomCount ||
			roomListResponse.capacities.size() != roomCount || roomListResponse.joinableFlags.size() != roomCount)
		{
			return false;
		}

		outCandidates.clear();
		outCandidates.reserve(roomCount);
		for (std::size_t index = 0; index < roomCount; ++index)
		{
			outCandidates.push_back({roomListResponse.roomIds[index],
				roomListResponse.roomNames[index],
				roomListResponse.participantCounts[index],
				roomListResponse.capacities[index],
				roomListResponse.joinableFlags[index] != 0});
		}

		return true;
	}

	std::uint64_t MakeSentTick() noexcept
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	const char* ToString(
		const ESessionState state) noexcept
	{
		switch (state)
		{
			case ESessionState::Idle:
				return "Idle";
			case ESessionState::WaitingLoginResponse:
				return "WaitingLoginResponse";
			case ESessionState::WaitingRoomListResponse:
				return "WaitingRoomListResponse";
			case ESessionState::WaitingRoomChangeResponse:
				return "WaitingRoomChangeResponse";
			case ESessionState::ConnectedIdle:
				return "ConnectedIdle";
			case ESessionState::WaitingChattingResponse:
				return "WaitingChattingResponse";
			case ESessionState::Disconnecting:
				return "Disconnecting";
			case ESessionState::Failed:
				return "Failed";
		}

		return "Unknown";
	}

	class FChattingDummyRuntime final
	{
	public:
		FChattingDummyRuntime(const SOptions& options, FRttMetricsRuntime* const rttMetricsRuntime);

		bool Run(std::string& outErrorMessage);

	private:
		static FClientNetworkConfig BuildClientNetworkConfig(const SOptions& options);
		bool AllSlotsTerminal() const;
		void BeginShutdown(std::chrono::steady_clock::time_point now);
		void IssueConnects(std::chrono::steady_clock::time_point now, std::string& outErrorMessage);
		void AttemptConnect(SSessionSlot& slot, std::chrono::steady_clock::time_point now, std::string& outErrorMessage);
		void ProcessScheduledActions(std::chrono::steady_clock::time_point now, std::string& outErrorMessage);
		void ProcessTimeouts(std::chrono::steady_clock::time_point now, FRttThreadLocalCollector& rttCollector);
		void DrainEvents(FRttThreadLocalCollector& rttCollector);
		void HandleDisconnected(const FClientEvent& event);
		void HandlePacketReceived(const FClientEvent& event, FRttThreadLocalCollector& rttCollector);
		void HandleLoginResponse(SSessionSlot& slot, const FClientEvent& event, FRttThreadLocalCollector& rttCollector);
		void HandleRoomListResponse(SSessionSlot& slot, const FClientEvent& event, FRttThreadLocalCollector& rttCollector);
		void HandleRoomChangeResponse(SSessionSlot& slot, const FClientEvent& event, FRttThreadLocalCollector& rttCollector);
		void HandleChattingResponse(SSessionSlot& slot, const FClientEvent& event, FRttThreadLocalCollector& rttCollector);
		void HandleBroadcast(SSessionSlot& slot, const FClientEvent& event);
		bool IsManagedDummyUserId(std::uint64_t userId) const noexcept;
		std::optional<std::uint32_t> SelectTargetRoom(SSessionSlot& slot);
		void RecordPendingSample(SSessionSlot& slot, FRttThreadLocalCollector& rttCollector);
		std::uint8_t MakeRandomKey(const SSessionSlot& slot, std::uint8_t salt) const noexcept;
		bool SendLogin(SSessionSlot& slot, std::string& outErrorMessage);
		bool SendRoomList(SSessionSlot& slot, std::string& outErrorMessage);
		bool SendRoomChange(SSessionSlot& slot, std::uint32_t targetRoomId, std::string& outErrorMessage);
		bool SendChatting(SSessionSlot& slot, std::string& outErrorMessage);
		void ScheduleReconnect(SSessionSlot& slot, std::chrono::steady_clock::time_point now, const std::string& reason, bool permanent);
		void MarkPermanentFailure(SSessionSlot& slot, const std::string& reason);
		void PrintSummary(std::chrono::steady_clock::time_point now);
		void PrintFinalSummary() const;

	private:
		SOptions m_options;
		FRttMetricsRuntime* m_rttMetricsRuntime = nullptr;
		FClientNetwork m_clientNetwork;
		std::vector<std::uint8_t> m_payloadPattern;
		std::vector<SSessionSlot> m_slots;
		std::unordered_map<FClientSessionId, int> m_sessionIndexById;
		SStats m_stats;
		std::chrono::steady_clock::time_point m_runStart = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point m_runDeadline = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point m_shutdownDeadline = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point m_nextSummaryTime = std::chrono::steady_clock::time_point::min();
		std::chrono::steady_clock::time_point m_nextConnectPermitTime = std::chrono::steady_clock::time_point::min();
		bool m_shutdownInitiated = false;
	};

	FChattingDummyRuntime::FChattingDummyRuntime(
		const SOptions& options,
		FRttMetricsRuntime* const rttMetricsRuntime)
		: m_options(options)
		, m_rttMetricsRuntime(rttMetricsRuntime)
		, m_clientNetwork(BuildClientNetworkConfig(options))
		, m_payloadPattern(BuildPayloadPattern(options.payloadSizeBytes, options.hiMode))
	{
		m_slots.reserve(static_cast<std::size_t>(std::max(1, options.sessionCount)));
		for (int sessionIndex = 0; sessionIndex < std::max(1, options.sessionCount); ++sessionIndex)
		{
			SSessionSlot slot{};
			slot.sessionIndex = sessionIndex;
			slot.userId = m_options.loginUserIdBase + static_cast<std::uint32_t>(sessionIndex);
			slot.randomEngine.seed(static_cast<std::uint32_t>(slot.userId * 2654435761u));
			slot.nextActionTime = std::chrono::steady_clock::time_point::min();
			m_slots.push_back(std::move(slot));
		}
	}

	bool FChattingDummyRuntime::Run(
		std::string& outErrorMessage)
	{
		if (!m_clientNetwork.Start(outErrorMessage))
		{
			return false;
		}

		FRttThreadLocalCollector rttCollector(m_rttMetricsRuntime);
		m_runStart = std::chrono::steady_clock::now();
		m_runDeadline = m_runStart + std::chrono::seconds(std::max(1, m_options.runSeconds));
		m_nextSummaryTime = m_runStart + std::chrono::seconds(std::max(1, m_options.consoleSummaryIntervalSeconds));
		m_nextConnectPermitTime = m_runStart;

		while (true)
		{
			const auto now = std::chrono::steady_clock::now();

			DrainEvents(rttCollector);
			ProcessTimeouts(now, rttCollector);

			if (!m_shutdownInitiated && now >= m_runDeadline)
			{
				BeginShutdown(now);
			}

			if (!m_shutdownInitiated)
			{
				IssueConnects(now, outErrorMessage);
				ProcessScheduledActions(now, outErrorMessage);
			}

			if (now >= m_nextSummaryTime)
			{
				PrintSummary(now);
				m_nextSummaryTime = now + std::chrono::seconds(std::max(1, m_options.consoleSummaryIntervalSeconds));
			}

			if (m_shutdownInitiated)
			{
				if (m_clientNetwork.GetActiveSessionCount() == 0 || now >= m_shutdownDeadline)
				{
					break;
				}
			}
			else if (AllSlotsTerminal())
			{
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(kSleepLoopMs));
		}

		m_clientNetwork.Stop();
		PrintFinalSummary();
		return m_stats.permanentFailureCount == 0 && m_stats.timeoutCount == 0 && m_stats.unexpectedDisconnectCount == 0 &&
			   m_stats.sessionErrorEventCount == 0 && m_stats.selfBroadcastCount == 0 && m_stats.invalidRoomBroadcastCount == 0 &&
			   m_stats.payloadValidationFailureCount == 0;
	}

	FClientNetworkConfig FChattingDummyRuntime::BuildClientNetworkConfig(
		const SOptions& options)
	{
		FClientNetworkConfig config{};
		config.ServerIp = options.serverIp;
		config.ServerPort = options.port;
		config.WorkerThreadCount = static_cast<std::uint32_t>(std::max(1, options.workerThreadCount));
		config.RecvScratchBufferSize = 8192;
		config.ValidatePacketChecksum = true;
		config.DisableNagle = true;
		config.PacketCipherConfig.enabled = true;
		config.PacketCipherConfig.packetKey = static_cast<std::uint8_t>(options.packetKey & 0xFFu);
		return config;
	}

	bool FChattingDummyRuntime::AllSlotsTerminal() const
	{
		for (const SSessionSlot& slot : m_slots)
		{
			if (!slot.permanentFailure && slot.state != ESessionState::Failed)
			{
				return false;
			}
		}

		return true;
	}

	void FChattingDummyRuntime::BeginShutdown(
		const std::chrono::steady_clock::time_point now)
	{
		m_shutdownInitiated = true;
		m_shutdownDeadline = now + std::chrono::seconds(5);
		for (SSessionSlot& slot : m_slots)
		{
			slot.reconnectRequested = false;
			slot.waitDeadline = std::chrono::steady_clock::time_point::max();
			slot.pendingRequest.reset();
			if (slot.sessionId == 0 || slot.state == ESessionState::Disconnecting)
			{
				continue;
			}

			m_clientNetwork.DisconnectSession(slot.sessionId, "benchmark shutdown");
			slot.state = ESessionState::Disconnecting;
		}
	}

	void FChattingDummyRuntime::IssueConnects(
		const std::chrono::steady_clock::time_point now,
		std::string& outErrorMessage)
	{
		while (true)
		{
			if (m_options.connectsPerSecond > 0 && now < m_nextConnectPermitTime)
			{
				return;
			}

			int connectableSlotIndex = -1;
			for (std::size_t index = 0; index < m_slots.size(); ++index)
			{
				SSessionSlot& slot = m_slots[index];
				if (slot.permanentFailure || slot.sessionId != 0 || slot.state != ESessionState::Idle || slot.nextActionTime > now)
				{
					continue;
				}

				connectableSlotIndex = static_cast<int>(index);
				break;
			}

			if (connectableSlotIndex < 0)
			{
				return;
			}

			AttemptConnect(m_slots[static_cast<std::size_t>(connectableSlotIndex)], now, outErrorMessage);
			if (m_options.connectsPerSecond > 0)
			{
				const auto interval = std::chrono::microseconds(std::max<std::int64_t>(1, 1000000LL / m_options.connectsPerSecond));
				m_nextConnectPermitTime = std::max(m_nextConnectPermitTime, now) + interval;
			}
		}
	}

	void FChattingDummyRuntime::AttemptConnect(
		SSessionSlot& slot,
		const std::chrono::steady_clock::time_point now,
		std::string& outErrorMessage)
	{
		++m_stats.connectAttemptCount;
		FClientSessionId sessionId = 0;
		if (!m_clientNetwork.ConnectSession(sessionId, outErrorMessage))
		{
			++m_stats.connectFailureCount;
			ScheduleReconnect(slot, now, "connect failed: " + outErrorMessage, false);
			return;
		}

		++m_stats.connectSuccessCount;
		slot.sessionId = sessionId;
		slot.state = ESessionState::Idle;
		slot.reconnectRequested = false;
		slot.waitDeadline = std::chrono::steady_clock::time_point::max();
		slot.pendingRequest.reset();
		m_sessionIndexById[sessionId] = slot.sessionIndex;

		if (!SendLogin(slot, outErrorMessage))
		{
			ScheduleReconnect(slot, now, outErrorMessage, false);
		}
	}

	void FChattingDummyRuntime::ProcessScheduledActions(
		const std::chrono::steady_clock::time_point now,
		std::string& outErrorMessage)
	{
		for (SSessionSlot& slot : m_slots)
		{
			if (slot.permanentFailure || slot.sessionId == 0 || slot.state != ESessionState::ConnectedIdle || slot.nextActionTime > now)
			{
				continue;
			}

			if (slot.currentRoomId.has_value())
			{
				if (!SendChatting(slot, outErrorMessage))
				{
					ScheduleReconnect(slot, now, outErrorMessage, false);
				}
			}
			else
			{
				if (!SendRoomList(slot, outErrorMessage))
				{
					ScheduleReconnect(slot, now, outErrorMessage, false);
				}
			}
		}
	}

	void FChattingDummyRuntime::ProcessTimeouts(
		const std::chrono::steady_clock::time_point now,
		FRttThreadLocalCollector& rttCollector)
	{
		for (SSessionSlot& slot : m_slots)
		{
			if (slot.permanentFailure || slot.sessionId == 0 || slot.waitDeadline == std::chrono::steady_clock::time_point::max() ||
				now < slot.waitDeadline)
			{
				continue;
			}

			++m_stats.timeoutCount;
			if (slot.pendingRequest.has_value())
			{
				rttCollector.RecordTimeout(slot.pendingRequest->stageIndex, std::chrono::system_clock::now());
				slot.pendingRequest.reset();
			}

			ScheduleReconnect(slot, now, std::string("response timeout in state=") + ToString(slot.state), false);
		}
	}

	void FChattingDummyRuntime::DrainEvents(
		FRttThreadLocalCollector& rttCollector)
	{
		std::vector<FClientEvent> events;
		const std::size_t maxEventCount = m_options.eventPollMaxCount <= 0 ? std::numeric_limits<std::size_t>::max()
																		   : static_cast<std::size_t>(m_options.eventPollMaxCount);
		const std::size_t reserveCount = m_options.eventPollMaxCount <= 0
											 ? static_cast<std::size_t>(1024)
											 : static_cast<std::size_t>(std::max(1, m_options.eventPollMaxCount));
		events.reserve(reserveCount);
		m_clientNetwork.PollEvents(events, maxEventCount);

		for (const FClientEvent& event : events)
		{
			switch (event.Type)
			{
				case EClientEventType::Connected:
					break;

				case EClientEventType::ConnectFailed:
					++m_stats.connectFailureCount;
					break;

				case EClientEventType::Disconnected:
					HandleDisconnected(event);
					break;

				case EClientEventType::PacketReceived:
					HandlePacketReceived(event, rttCollector);
					break;

				case EClientEventType::SendFailed:
				case EClientEventType::SessionError:
					++m_stats.sessionErrorEventCount;
					break;
			}
		}
	}

	void FChattingDummyRuntime::HandleDisconnected(
		const FClientEvent& event)
	{
		const auto sessionIt = m_sessionIndexById.find(event.SessionId);
		if (sessionIt == m_sessionIndexById.end())
		{
			return;
		}

		SSessionSlot& slot = m_slots[static_cast<std::size_t>(sessionIt->second)];
		m_sessionIndexById.erase(sessionIt);
		slot.sessionId = 0;
		slot.roomCandidates.clear();
		slot.currentRoomId.reset();
		slot.waitDeadline = std::chrono::steady_clock::time_point::max();
		slot.pendingRequest.reset();

		if (slot.permanentFailure)
		{
			slot.state = ESessionState::Failed;
			return;
		}

		if (m_shutdownInitiated)
		{
			slot.state = ESessionState::Idle;
			slot.nextActionTime = std::chrono::steady_clock::time_point::max();
			return;
		}

		if (!slot.reconnectRequested)
		{
			++m_stats.unexpectedDisconnectCount;
		}

		slot.state = ESessionState::Idle;
		slot.nextActionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, m_options.reconnectDelayMs));
		slot.reconnectRequested = false;
		++m_stats.reconnectCount;
	}

	void FChattingDummyRuntime::HandlePacketReceived(
		const FClientEvent& event,
		FRttThreadLocalCollector& rttCollector)
	{
		const auto sessionIt = m_sessionIndexById.find(event.SessionId);
		if (sessionIt == m_sessionIndexById.end())
		{
			return;
		}

		SSessionSlot& slot = m_slots[static_cast<std::size_t>(sessionIt->second)];
		m_stats.receivePayloadBytes += static_cast<std::uint64_t>(event.Packet.Payload.size());

		switch (event.Packet.Opcode)
		{
			case Generated::Login::FLoginRp::kOpcode:
				HandleLoginResponse(slot, event, rttCollector);
				return;

			case Generated::Chatting::FRoomListRp::kOpcode:
				HandleRoomListResponse(slot, event, rttCollector);
				return;

			case Generated::Chatting::FRoomChangeRp::kOpcode:
				HandleRoomChangeResponse(slot, event, rttCollector);
				return;

			case Generated::Chatting::FChattingRp::kOpcode:
				HandleChattingResponse(slot, event, rttCollector);
				return;

			case Generated::Chatting::FBroadcast::kOpcode:
				HandleBroadcast(slot, event);
				return;

			default:
				MarkPermanentFailure(slot, "unexpected opcode=" + std::to_string(event.Packet.Opcode));
				return;
		}
	}

	void FChattingDummyRuntime::HandleLoginResponse(
		SSessionSlot& slot,
		const FClientEvent& event,
		FRttThreadLocalCollector& rttCollector)
	{
		if (slot.state != ESessionState::WaitingLoginResponse)
		{
			MarkPermanentFailure(slot, "login response arrived in unexpected state");
			return;
		}

		Generated::Login::FLoginRp responsePacket;
		if (!ClientNetworkLib::TryDeserializePacketEvent(event, responsePacket))
		{
			MarkPermanentFailure(slot, "login response deserialize failed");
			return;
		}

		RecordPendingSample(slot, rttCollector);
		if (!responsePacket.success || responsePacket.userId != slot.userId)
		{
			MarkPermanentFailure(slot, "login response validation failed");
			return;
		}

		++m_stats.loginSuccessCount;
		std::string errorMessage;
		if (!SendRoomList(slot, errorMessage))
		{
			ScheduleReconnect(slot, std::chrono::steady_clock::now(), errorMessage, false);
		}
	}

	void FChattingDummyRuntime::HandleRoomListResponse(
		SSessionSlot& slot,
		const FClientEvent& event,
		FRttThreadLocalCollector& rttCollector)
	{
		if (slot.state != ESessionState::WaitingRoomListResponse)
		{
			MarkPermanentFailure(slot, "room list response arrived in unexpected state");
			return;
		}

		Generated::Chatting::FRoomListRp responsePacket;
		if (!ClientNetworkLib::TryDeserializePacketEvent(event, responsePacket))
		{
			MarkPermanentFailure(slot, "room list response deserialize failed");
			return;
		}

		RecordPendingSample(slot, rttCollector);
		++m_stats.roomListResponseCount;

		if (!TryBuildRoomCandidates(responsePacket, slot.roomCandidates))
		{
			MarkPermanentFailure(slot, "room list response validation failed");
			return;
		}

		const std::optional<std::uint32_t> targetRoomId = SelectTargetRoom(slot);
		if (!targetRoomId.has_value())
		{
			slot.state = ESessionState::ConnectedIdle;
			slot.nextActionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, m_options.reconnectDelayMs));
			return;
		}

		std::string errorMessage;
		if (!SendRoomChange(slot, *targetRoomId, errorMessage))
		{
			ScheduleReconnect(slot, std::chrono::steady_clock::now(), errorMessage, false);
		}
	}

	void FChattingDummyRuntime::HandleRoomChangeResponse(
		SSessionSlot& slot,
		const FClientEvent& event,
		FRttThreadLocalCollector& rttCollector)
	{
		if (slot.state != ESessionState::WaitingRoomChangeResponse)
		{
			MarkPermanentFailure(slot, "room change response arrived in unexpected state");
			return;
		}

		Generated::Chatting::FRoomChangeRp responsePacket;
		if (!ClientNetworkLib::TryDeserializePacketEvent(event, responsePacket))
		{
			MarkPermanentFailure(slot, "room change response deserialize failed");
			return;
		}

		RecordPendingSample(slot, rttCollector);

		if (responsePacket.success)
		{
			++m_stats.roomChangeSuccessCount;
			slot.currentRoomId = responsePacket.currentRoomId;
			slot.state = ESessionState::ConnectedIdle;
			slot.nextActionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_options.sendIntervalMs);
			slot.recoverableErrorCount = 0;
			return;
		}

		++m_stats.roomChangeFailureCount;
		if (responsePacket.currentRoomId != 0)
		{
			slot.currentRoomId = responsePacket.currentRoomId;
			slot.state = ESessionState::ConnectedIdle;
			slot.nextActionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_options.sendIntervalMs);
			return;
		}

		slot.currentRoomId.reset();
		slot.state = ESessionState::ConnectedIdle;
		slot.nextActionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, m_options.reconnectDelayMs));
	}

	void FChattingDummyRuntime::HandleChattingResponse(
		SSessionSlot& slot,
		const FClientEvent& event,
		FRttThreadLocalCollector& rttCollector)
	{
		if (slot.state != ESessionState::WaitingChattingResponse)
		{
			MarkPermanentFailure(slot, "chatting response arrived in unexpected state");
			return;
		}

		Generated::Chatting::FChattingRp responsePacket;
		if (!ClientNetworkLib::TryDeserializePacketEvent(event, responsePacket))
		{
			MarkPermanentFailure(slot, "chatting response deserialize failed");
			return;
		}

		RecordPendingSample(slot, rttCollector);
		if (!responsePacket.success)
		{
			++m_stats.chattingRejectCount;
			MarkPermanentFailure(slot, "chatting response rejected request");
			return;
		}

		++m_stats.chattingSuccessCount;
		const auto now = std::chrono::steady_clock::now();
		if (ShouldAttemptAction(m_options.reconnectProbabilityPercent, slot.randomEngine))
		{
			slot.reconnectRequested = true;
			m_clientNetwork.DisconnectSession(slot.sessionId, "probabilistic reconnect");
			slot.state = ESessionState::Disconnecting;
			return;
		}

		if (ShouldAttemptAction(m_options.roomChangeProbabilityPercent, slot.randomEngine))
		{
			std::string errorMessage;
			if (!SendRoomList(slot, errorMessage))
			{
				ScheduleReconnect(slot, now, errorMessage, false);
			}
			return;
		}

		slot.state = ESessionState::ConnectedIdle;
		slot.nextActionTime = now + std::chrono::milliseconds(m_options.sendIntervalMs);
	}

	void FChattingDummyRuntime::HandleBroadcast(
		SSessionSlot& slot,
		const FClientEvent& event)
	{
		if (!slot.currentRoomId.has_value())
		{
			MarkPermanentFailure(slot, "broadcast received while not joined to a room");
			return;
		}

		Generated::Chatting::FBroadcast broadcastPacket;
		if (!ClientNetworkLib::TryDeserializePacketEvent(event, broadcastPacket))
		{
			MarkPermanentFailure(slot, "broadcast deserialize failed");
			return;
		}

		if (broadcastPacket.roomId != *slot.currentRoomId)
		{
			++m_stats.invalidRoomBroadcastCount;
			MarkPermanentFailure(slot, "broadcast room id mismatch");
			return;
		}

		if (broadcastPacket.senderUserId == slot.userId)
		{
			++m_stats.selfBroadcastCount;
			MarkPermanentFailure(slot, "self broadcast detected");
			return;
		}

		if (IsManagedDummyUserId(broadcastPacket.senderUserId) && broadcastPacket.payload != m_payloadPattern)
		{
			++m_stats.payloadValidationFailureCount;
			MarkPermanentFailure(slot, "broadcast payload validation failed");
			return;
		}

		++m_stats.broadcastReceiveCount;
	}

	bool FChattingDummyRuntime::IsManagedDummyUserId(
		const std::uint64_t userId) const noexcept
	{
		const std::uint64_t begin = static_cast<std::uint64_t>(m_options.loginUserIdBase);
		const std::uint64_t end = begin + static_cast<std::uint64_t>(std::max(1, m_options.sessionCount));
		return userId >= begin && userId < end;
	}

	std::optional<std::uint32_t> FChattingDummyRuntime::SelectTargetRoom(
		SSessionSlot& slot)
	{
		std::vector<std::uint32_t> hotspotCandidates;
		std::vector<std::uint32_t> otherCandidates;

		for (const SRoomCandidate& roomCandidate : slot.roomCandidates)
		{
			if (!roomCandidate.joinable)
			{
				continue;
			}

			if (slot.currentRoomId.has_value() && roomCandidate.roomId == *slot.currentRoomId)
			{
				continue;
			}

			const bool isHotspotRoom = std::find(m_options.hotspotRoomIds.begin(), m_options.hotspotRoomIds.end(), roomCandidate.roomId) !=
									   m_options.hotspotRoomIds.end();
			if (isHotspotRoom)
			{
				hotspotCandidates.push_back(roomCandidate.roomId);
			}
			else
			{
				otherCandidates.push_back(roomCandidate.roomId);
			}
		}

		std::vector<std::uint32_t> allCandidates = hotspotCandidates;
		allCandidates.insert(allCandidates.end(), otherCandidates.begin(), otherCandidates.end());
		if (allCandidates.empty())
		{
			return std::nullopt;
		}

		switch (m_options.roomSelectionMode)
		{
			case Generated::Config::ChattingDummy::ERoomSelectionMode::Random:
			{
				std::uniform_int_distribution<std::size_t> distribution(0, allCandidates.size() - 1);
				return allCandidates[distribution(slot.randomEngine)];
			}

			case Generated::Config::ChattingDummy::ERoomSelectionMode::RoundRobin:
			{
				const std::size_t selectedIndex = slot.roundRobinCursor % allCandidates.size();
				slot.roundRobinCursor = (selectedIndex + 1) % allCandidates.size();
				return allCandidates[selectedIndex];
			}

			case Generated::Config::ChattingDummy::ERoomSelectionMode::Hotspot:
			{
				const bool chooseHotspot =
					!hotspotCandidates.empty() && ShouldAttemptAction(m_options.hotspotBiasPercent, slot.randomEngine);
				const std::vector<std::uint32_t>& targetCandidates =
					chooseHotspot ? hotspotCandidates : (!otherCandidates.empty() ? otherCandidates : allCandidates);
				std::uniform_int_distribution<std::size_t> distribution(0, targetCandidates.size() - 1);
				return targetCandidates[distribution(slot.randomEngine)];
			}
		}

		return allCandidates.front();
	}

	void FChattingDummyRuntime::RecordPendingSample(
		SSessionSlot& slot,
		FRttThreadLocalCollector& rttCollector)
	{
		slot.waitDeadline = std::chrono::steady_clock::time_point::max();
		if (!slot.pendingRequest.has_value())
		{
			return;
		}

		rttCollector.RecordSample(*slot.pendingRequest, std::chrono::system_clock::now());
		slot.pendingRequest.reset();
	}

	std::uint8_t FChattingDummyRuntime::MakeRandomKey(
		const SSessionSlot& slot,
		const std::uint8_t salt) const noexcept
	{
		return static_cast<std::uint8_t>((slot.userId + slot.nextClientMessageId + salt) & 0xFFu);
	}

	bool FChattingDummyRuntime::SendLogin(
		SSessionSlot& slot,
		std::string& outErrorMessage)
	{
		Generated::Login::FLoginRq requestPacket;
		requestPacket.userId = slot.userId;

		slot.pendingRequest = MakePendingRequest(ERttStage::LoginResponse, slot.sessionIndex);
		slot.waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, m_options.responseTimeoutMs));
		slot.state = ESessionState::WaitingLoginResponse;
		return m_clientNetwork.SendPacket(slot.sessionId, requestPacket, MakeRandomKey(slot, 0x21), outErrorMessage);
	}

	bool FChattingDummyRuntime::SendRoomList(
		SSessionSlot& slot,
		std::string& outErrorMessage)
	{
		Generated::Chatting::FRoomListRq requestPacket;
		slot.pendingRequest = MakePendingRequest(ERttStage::RoomList, slot.sessionIndex);
		slot.waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, m_options.responseTimeoutMs));
		slot.state = ESessionState::WaitingRoomListResponse;
		return m_clientNetwork.SendPacket(slot.sessionId, requestPacket, MakeRandomKey(slot, 0x41), outErrorMessage);
	}

	bool FChattingDummyRuntime::SendRoomChange(
		SSessionSlot& slot,
		const std::uint32_t targetRoomId,
		std::string& outErrorMessage)
	{
		Generated::Chatting::FRoomChangeRq requestPacket;
		requestPacket.targetRoomId = targetRoomId;

		slot.pendingRequest = MakePendingRequest(ERttStage::RoomChange, slot.sessionIndex);
		slot.waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, m_options.responseTimeoutMs));
		slot.state = ESessionState::WaitingRoomChangeResponse;
		return m_clientNetwork.SendPacket(slot.sessionId, requestPacket, MakeRandomKey(slot, 0x61), outErrorMessage);
	}

	bool FChattingDummyRuntime::SendChatting(
		SSessionSlot& slot,
		std::string& outErrorMessage)
	{
		if (!slot.currentRoomId.has_value())
		{
			outErrorMessage = "current room id is not assigned.";
			return false;
		}

		Generated::Chatting::FChattingRq requestPacket;
		requestPacket.roomId = *slot.currentRoomId;
		requestPacket.clientMessageId = slot.nextClientMessageId++;
		requestPacket.sentTick = MakeSentTick();
		requestPacket.payload = m_payloadPattern;

		slot.pendingRequest = MakePendingRequest(ERttStage::ChattingResponse, slot.sessionIndex);
		slot.waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, m_options.responseTimeoutMs));
		slot.state = ESessionState::WaitingChattingResponse;
		++m_stats.chattingSendCount;
		m_stats.sendPayloadBytes += static_cast<std::uint64_t>(requestPacket.payload.size());
		return m_clientNetwork.SendPacket(slot.sessionId, requestPacket, MakeRandomKey(slot, 0x81), outErrorMessage);
	}

	void FChattingDummyRuntime::ScheduleReconnect(
		SSessionSlot& slot,
		const std::chrono::steady_clock::time_point now,
		const std::string& reason,
		const bool permanent)
	{
		slot.lastErrorMessage = reason;
		slot.pendingRequest.reset();
		slot.waitDeadline = std::chrono::steady_clock::time_point::max();

		if (permanent)
		{
			MarkPermanentFailure(slot, reason);
			return;
		}

		++slot.recoverableErrorCount;
		if (slot.recoverableErrorCount > kMaxRecoverableErrorCount)
		{
			MarkPermanentFailure(slot, reason);
			return;
		}

		if (slot.sessionId == 0)
		{
			slot.state = ESessionState::Idle;
			slot.nextActionTime = now + std::chrono::milliseconds(std::max(0, m_options.reconnectDelayMs));
			return;
		}

		slot.reconnectRequested = true;
		slot.state = ESessionState::Disconnecting;
		m_clientNetwork.DisconnectSession(slot.sessionId, reason);
	}

	void FChattingDummyRuntime::MarkPermanentFailure(
		SSessionSlot& slot,
		const std::string& reason)
	{
		if (slot.permanentFailure)
		{
			return;
		}

		slot.permanentFailure = true;
		slot.lastErrorMessage = reason;
		++m_stats.permanentFailureCount;

		std::cerr << "session[" << slot.sessionIndex << "] permanent failure. state=" << ToString(slot.state) << " userId=" << slot.userId
				  << " error=" << reason << "\n";

		if (slot.sessionId == 0)
		{
			slot.state = ESessionState::Failed;
			return;
		}

		slot.reconnectRequested = false;
		slot.state = ESessionState::Disconnecting;
		m_clientNetwork.DisconnectSession(slot.sessionId, reason);
	}

	void FChattingDummyRuntime::PrintSummary(
		const std::chrono::steady_clock::time_point now)
	{
		const double elapsedSeconds = std::max(0.001, std::chrono::duration<double>(now - m_runStart).count());

		std::size_t activeSessions = 0;
		std::size_t failedSessions = 0;
		for (const SSessionSlot& slot : m_slots)
		{
			if (slot.sessionId != 0)
			{
				++activeSessions;
			}

			if (slot.permanentFailure)
			{
				++failedSessions;
			}
		}

		std::cout << "[summary] elapsed=" << std::fixed << std::setprecision(1) << elapsedSeconds << "s"
				  << " activeSessions=" << activeSessions << " failedSessions=" << failedSessions
				  << " connect=" << m_stats.connectSuccessCount << " login=" << m_stats.loginSuccessCount
				  << " roomChangeOk=" << m_stats.roomChangeSuccessCount << " chatOk=" << m_stats.chattingSuccessCount
				  << " broadcast=" << m_stats.broadcastReceiveCount << " reconnect=" << m_stats.reconnectCount
				  << " timeout=" << m_stats.timeoutCount << " sendBytes=" << m_stats.sendPayloadBytes
				  << " recvBytes=" << m_stats.receivePayloadBytes << "\n";
	}

	void FChattingDummyRuntime::PrintFinalSummary() const
	{
		const double elapsedSeconds = std::max(0.001, std::chrono::duration<double>(std::chrono::steady_clock::now() - m_runStart).count());

		std::cout << "chatting dummy finished. "
				  << "elapsedSeconds=" << std::fixed << std::setprecision(2) << elapsedSeconds
				  << " connectSuccess=" << m_stats.connectSuccessCount << " loginSuccess=" << m_stats.loginSuccessCount
				  << " roomListResponses=" << m_stats.roomListResponseCount << " roomChangeSuccess=" << m_stats.roomChangeSuccessCount
				  << " roomChangeFailure=" << m_stats.roomChangeFailureCount << " chattingSend=" << m_stats.chattingSendCount
				  << " chattingSuccess=" << m_stats.chattingSuccessCount << " chattingReject=" << m_stats.chattingRejectCount
				  << " broadcastReceive=" << m_stats.broadcastReceiveCount << " reconnect=" << m_stats.reconnectCount
				  << " unexpectedDisconnect=" << m_stats.unexpectedDisconnectCount << " timeout=" << m_stats.timeoutCount
				  << " sessionError=" << m_stats.sessionErrorEventCount << " selfBroadcast=" << m_stats.selfBroadcastCount
				  << " invalidRoomBroadcast=" << m_stats.invalidRoomBroadcastCount
				  << " payloadValidationFailure=" << m_stats.payloadValidationFailureCount
				  << " permanentFailure=" << m_stats.permanentFailureCount << "\n";
	}
}

int main(
	int argc,
	char* argv[])
{
	const std::filesystem::path executableDirectory = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
	const std::filesystem::path configPath =
		TryGetConfigPathOverride(argc, argv).value_or(ResolveDefaultChattingDummyConfigPath(executableDirectory));

	Generated::Config::ChattingDummy::FChattingDummyConfigDocument configDocument{};
	std::string configErrorMessage;
	if (!Generated::Config::ChattingDummy::FChattingDummyConfigLoader::LoadFromFile(configPath, configDocument, configErrorMessage))
	{
		std::cerr << "ChattingDummy config load failed: " << configErrorMessage << "\n";
		return 1;
	}

	SOptions options{};
	ApplyChattingDummyConfigDocument(configDocument, executableDirectory, options);

	NetworkLib::Packet::Buffer::FPacketBuffer::ConfigurePageReuse(true, kDefaultPageSize);

	std::unique_ptr<FRttMetricsRuntime> rttMetricsRuntime;
	std::unique_ptr<FRttCsvLogger> rttCsvLogger;
	if (!options.rttCsvPath.empty())
	{
		rttMetricsRuntime = std::make_unique<FRttMetricsRuntime>(BuildRttMetricsConfig());
		rttCsvLogger = std::make_unique<FRttCsvLogger>(*rttMetricsRuntime, options.rttCsvPath);
		rttCsvLogger->Start();
	}

	std::string runErrorMessage;
	FChattingDummyRuntime runtime(options, rttMetricsRuntime.get());
	const bool runSucceeded = runtime.Run(runErrorMessage);

	if (rttCsvLogger)
	{
		rttCsvLogger->Stop();
	}

	if (!runSucceeded)
	{
		if (!runErrorMessage.empty())
		{
			std::cerr << "ChattingDummy failed: " << runErrorMessage << "\n";
		}
		return 1;
	}

	return 0;
}
