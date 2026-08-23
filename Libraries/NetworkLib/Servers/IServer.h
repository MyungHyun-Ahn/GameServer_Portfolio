#pragma once

namespace NetworkLib
{
	namespace Core
	{
		enum class EBackendKind : std::uint32_t;
		struct SServerConfig;
		struct SServerStats;
	}

	namespace Packet::Serialization
	{
		class FOutgoingContentPacket;
	}

	class IApplicationHandler;

	class IServer
	{
	public:
		virtual ~IServer() = default;

		virtual bool Start(const Core::SServerConfig& serverConfig, IApplicationHandler& applicationHandler) = 0;
		virtual void Stop() = 0;
		virtual bool SendPacket(std::uint64_t sessionId, NetworkLib::Packet::Serialization::FOutgoingContentPacket&& packet) = 0;
		virtual bool Disconnect(std::uint64_t sessionId) = 0;
		virtual Core::EBackendKind GetBackendKind() const = 0;
		virtual Core::SServerStats GetStatsSnapshot() const = 0;
	};
}
