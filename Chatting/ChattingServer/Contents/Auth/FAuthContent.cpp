#include "ChattingServerPch.h"

#include "ChattingServer/Contents/Auth/FAuthContent.h"

#include "ChattingServer/Contents/Session/FUserRegistry.h"
#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "Generated/Packets/Cpp/Login/LoginPackets.h"

#include <format>
namespace ChattingServer::Contents
{
	FAuthContent::FAuthContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		std::shared_ptr<FUserRegistry> userRegistry,
		std::shared_ptr<Connector::IChatTicketStore> chatTicketStore,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
		, m_userRegistry(std::move(userRegistry))
		, m_chatTicketStore(std::move(chatTicketStore))
		, m_runtimeOptions(std::move(runtimeOptions))
	{
	}

	ContentsRuntime::Core::FContentId FAuthContent::GetContentId() const noexcept
	{
		return kAuthContentId;
	}

	ContentsRuntime::Core::FContentInstanceId FAuthContent::GetContentInstanceId() const noexcept
	{
		return m_contentInstanceId;
	}

	void FAuthContent::OnEnter(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		m_sessionGenerations[sessionId] = routeGeneration;
		Log(Foundation::ELogLevel::Info, "auth content enter. sessionId={} routeGeneration={}", sessionId, routeGeneration);
	}

	void FAuthContent::OnLeave(
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		ContentsRuntime::Bridge::IContentBridge&)
	{
		const auto generationIt = m_sessionGenerations.find(sessionId);
		const bool isCurrentGeneration = generationIt != m_sessionGenerations.end() && generationIt->second == routeGeneration;
		Log(Foundation::ELogLevel::Info,
			"auth content leave. sessionId={} routeGeneration={} stale={}",
			sessionId,
			routeGeneration,
			(isCurrentGeneration ? 0 : 1));
		if (isCurrentGeneration)
		{
			m_sessionGenerations.erase(sessionId);
		}
	}

	void FAuthContent::OnPacket(
		const std::uint64_t sessionId,
		const std::uint64_t routeGeneration,
		const std::uint16_t opcode,
		std::span<const char> payload,
		ContentsRuntime::Bridge::IContentBridge& bridge)
	{
		const auto currentContentInstanceId = bridge.GetCurrentContentInstanceId(sessionId);
		if (!currentContentInstanceId.has_value() || *currentContentInstanceId != m_contentInstanceId)
		{
			return;
		}

		const auto generationIt = m_sessionGenerations.find(sessionId);
		if (generationIt == m_sessionGenerations.end() || generationIt->second != routeGeneration)
		{
			return;
		}

		if (opcode == Generated::Login::FLoginRq::kOpcode)
		{
			Generated::Login::FLoginRq requestPacket;
			if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, requestPacket))
			{
				Log(Foundation::ELogLevel::Warn, "login deserialize failed.");
				return;
			}

			if (!m_runtimeOptions.allowLegacyLogin)
			{
				Generated::Login::FLoginRp responsePacket;
				responsePacket.userId = requestPacket.userId;
				responsePacket.success = false;
				if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
				{
					Log(Foundation::ELogLevel::Error,
						"legacy login rejection response send failed. sessionId={} userId={}",
						sessionId,
						requestPacket.userId);
					return;
				}

				Log(Foundation::ELogLevel::Warn,
					"legacy login rejected by authentication policy. sessionId={} userId={}",
					sessionId,
					requestPacket.userId);
				return;
			}

			bool success = requestPacket.userId != 0;
			if (success && m_userRegistry != nullptr)
			{
				m_userRegistry->UpsertUser(sessionId, requestPacket.userId);
			}

			if (success && !bridge.MoveSession(sessionId, kLobbyContentId))
			{
				Log(Foundation::ELogLevel::Error, "move to lobby content failed. sessionId={} userId={}", sessionId, requestPacket.userId);
				success = false;
				if (m_userRegistry != nullptr)
				{
					m_userRegistry->RemoveUser(sessionId);
				}
			}

			Generated::Login::FLoginRp responsePacket;
			responsePacket.userId = requestPacket.userId;
			responsePacket.success = success;
			if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
			{
				if (success && m_userRegistry != nullptr)
				{
					m_userRegistry->RemoveUser(sessionId);
				}

				Log(Foundation::ELogLevel::Error, "login response send failed. sessionId={} userId={}", sessionId, requestPacket.userId);
				return;
			}

			if (!success)
			{
				if (m_userRegistry != nullptr)
				{
					m_userRegistry->RemoveUser(sessionId);
				}

				Log(Foundation::ELogLevel::Warn, "login rejected. sessionId={} userId={}", sessionId, requestPacket.userId);
				return;
			}

			if (m_runtimeOptions.bootstrapTrace && m_runtimeOptions.traceUserId != 0 &&
				requestPacket.userId == m_runtimeOptions.traceUserId && m_runtimeOptions.tracedSessionId != nullptr)
			{
				m_runtimeOptions.tracedSessionId->store(sessionId, std::memory_order_relaxed);
				Log(Foundation::ELogLevel::Info, "bootstrap trace target mapped. userId={} sessionId={}", requestPacket.userId, sessionId);
			}

			Log(Foundation::ELogLevel::Info, "legacy login succeeded. sessionId={} userId={}", sessionId, requestPacket.userId);
			return;
		}

		if (opcode != Generated::Login::FLoginAuthRq::kOpcode)
		{
			return;
		}

		Generated::Login::FLoginAuthRq requestPacket;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, requestPacket))
		{
			Log(Foundation::ELogLevel::Warn, "login auth deserialize failed.");
			return;
		}

		Generated::Login::FLoginAuthRp responsePacket;
		Connector::SConsumedChatTicket consumedTicket{};
		std::string authError;
		bool success = false;
		std::optional<std::uint64_t> previousSessionId;
		if (m_chatTicketStore == nullptr)
		{
			authError = "chat ticket store is not configured.";
		}
		else
		{
			success = m_chatTicketStore->TryConsumeChatTicket(requestPacket.ticket, consumedTicket, authError);
		}

		if (success && (!consumedTicket.valid || consumedTicket.userId == 0))
		{
			success = false;
			authError = "chat ticket payload is invalid.";
		}

		if (success && m_userRegistry != nullptr)
		{
			previousSessionId = m_userRegistry->GetSessionId(consumedTicket.userId);
		}

		if (success && !bridge.MoveSession(sessionId, kLobbyContentId))
		{
			success = false;
			authError = "move to lobby content failed.";
		}

		if (success && m_userRegistry != nullptr)
		{
			m_userRegistry->UpsertUser(sessionId, consumedTicket.userId);
		}

		if (success && previousSessionId.has_value() && *previousSessionId != sessionId && bridge.IsSessionAlive(*previousSessionId))
		{
			if (!bridge.DisconnectSession(*previousSessionId))
			{
				Log(Foundation::ELogLevel::Warn,
					"duplicate login disconnect failed. userId={} previousSessionId={} replacementSessionId={}",
					consumedTicket.userId,
					*previousSessionId,
					sessionId);
			}
			else
			{
				Log(Foundation::ELogLevel::Info,

					"duplicate login replaced existing session. userId={} previousSessionId={} replacementSessionId={} loginVersion={}",
					consumedTicket.userId,
					*previousSessionId,
					sessionId,
					consumedTicket.loginVersion);
			}
		}

		responsePacket.userId = success ? consumedTicket.userId : 0;
		responsePacket.success = success;
		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			if (success && m_userRegistry != nullptr)
			{
				m_userRegistry->RemoveUser(sessionId);
			}

			Log(Foundation::ELogLevel::Error, "login auth response send failed. sessionId={}", sessionId);
			return;
		}

		if (!success)
		{
			if (m_userRegistry != nullptr)
			{
				m_userRegistry->RemoveUser(sessionId);
			}

			Log(Foundation::ELogLevel::Warn, "login auth rejected. sessionId={} reason={}", sessionId, authError);
			return;
		}

		if (m_runtimeOptions.bootstrapTrace && m_runtimeOptions.traceUserId != 0 && consumedTicket.userId == m_runtimeOptions.traceUserId &&
			m_runtimeOptions.tracedSessionId != nullptr)
		{
			m_runtimeOptions.tracedSessionId->store(sessionId, std::memory_order_relaxed);
			Log(Foundation::ELogLevel::Info, "bootstrap trace target mapped. userId={} sessionId={}", consumedTicket.userId, sessionId);
		}

		Log(Foundation::ELogLevel::Info,
			"login auth succeeded. sessionId={} userId={} loginVersion={}",
			sessionId,
			consumedTicket.userId,
			consumedTicket.loginVersion);
	}

	void FAuthContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "ChattingServer", message);
		}
	}
}
