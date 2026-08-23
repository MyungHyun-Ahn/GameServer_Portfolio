#pragma once

namespace ClientNetworkLib
{
	using FClientSessionId = std::uint64_t;

	struct FClientNetworkConfig final
	{
		std::string ServerIp = "127.0.0.1";
		std::uint16_t ServerPort = 0;
		std::uint32_t WorkerThreadCount = 4;
		std::size_t RecvScratchBufferSize = 8192;
		bool ValidatePacketChecksum = true;
		bool DisableNagle = true;
		NetworkLib::Crypto::SDefaultPacketCipherConfig PacketCipherConfig{};
	};

	enum class EClientEventType : std::uint8_t
	{
		Connected,
		ConnectFailed,
		Disconnected,
		PacketReceived,
		SendFailed,
		SessionError
	};

	struct FClientPacketData final
	{
		std::uint16_t Opcode = 0;
		std::uint8_t RandomKey = 0;
		std::uint8_t Checksum = 0;
		std::vector<char> Payload;

		bool HasPayload() const noexcept
		{
			return !Payload.empty();
		}
	};

	struct FClientEvent final
	{
		EClientEventType Type = EClientEventType::SessionError;
		FClientSessionId SessionId = 0;
		int ErrorCode = 0;
		std::string Message;
		std::chrono::steady_clock::time_point TimestampSteady = std::chrono::steady_clock::time_point::min();
		std::chrono::system_clock::time_point TimestampSystem = std::chrono::system_clock::time_point::min();
		FClientPacketData Packet;

		bool IsPacketReceived() const noexcept
		{
			return Type == EClientEventType::PacketReceived;
		}
	};

	template <typename TPacket>
	bool TryDeserializePacketEvent(
		const FClientEvent& event,
		TPacket& outPacket)
	{
		if (event.Type != EClientEventType::PacketReceived || event.Packet.Opcode != TPacket::kOpcode)
		{
			return false;
		}

		return NetworkLib::Packet::Serialization::DeserializeContentPacket(
			event.Packet.Payload.data(), event.Packet.Payload.size(), outPacket);
	}
}
