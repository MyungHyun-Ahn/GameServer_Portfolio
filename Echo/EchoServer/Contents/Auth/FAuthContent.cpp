#include "EchoServerPch.h"

#include "EchoServer/Contents/Auth/FAuthContent.h"

#include "ContentsRuntime/Bridge/IContentBridge.h"
#include "EchoServer/Contents/ContentTypes.h"
#include "Generated/Packets/Chat/ChatPackets.h"
#include "Generated/Packets/Login/LoginPackets.h"

#include <format>
namespace EchoServer::Contents
{
	FAuthContent::FAuthContent(
		std::shared_ptr<Foundation::ILogger> logger,
		const ContentsRuntime::Core::FContentInstanceId contentInstanceId,
		SRuntimeOptions runtimeOptions)
		: m_logger(std::move(logger))
		, m_contentInstanceId(contentInstanceId)
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
		std::uint64_t sessionId,
		std::uint64_t routeGeneration,
		std::uint16_t opcode,
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

		if (opcode != Generated::Login::FLoginRq::kOpcode)
			return;

		Generated::Login::FLoginRq packet;
		if (!ContentsRuntime::Bridge::DeserializeOwnedPacket(opcode, payload, packet))
		{
			Log(Foundation::ELogLevel::Warn, "login deserialize failed.");
			return;
		}

		const bool success = packet.userId != 0;
		{
			Log(success ? Foundation::ELogLevel::Info : Foundation::ELogLevel::Warn,
				"login {}. sessionId={} userId={}",
				(success ? "succeeded" : "failed"),
				sessionId,
				packet.userId);
		}

		if (success && m_runtimeOptions.bootstrapTrace && m_runtimeOptions.traceUserId != 0 &&
			packet.userId == m_runtimeOptions.traceUserId && m_runtimeOptions.tracedSessionId != nullptr)
		{
			m_runtimeOptions.tracedSessionId->store(sessionId, std::memory_order_relaxed);
			Log(Foundation::ELogLevel::Info, "bootstrap trace target mapped. userId={} sessionId={}", packet.userId, sessionId);
		}

		Generated::Login::FLoginRp responsePacket;
		responsePacket.userId = packet.userId;
		responsePacket.success = success;
		if (success)
		{
			const bool moveSucceeded = bridge.MoveSession(sessionId, kLobbyContentId);
			if (!moveSucceeded)
			{
				Log(Foundation::ELogLevel::Error, "move to lobby content failed. sessionId={} userId={}", sessionId, packet.userId);
				return;
			}
		}

		if (!ContentsRuntime::Bridge::SendContentPacket(bridge, sessionId, responsePacket))
		{
			Log(Foundation::ELogLevel::Error, "login response send failed. sessionId={} userId={}", sessionId, packet.userId);
			return;
		}
	}

	void FAuthContent::Log(
		Foundation::ELogLevel logLevel,
		const std::string& message) const
	{
		if (m_logger != nullptr)
		{
			m_logger->Log(logLevel, "EchoServer", message);
		}
	}
}
