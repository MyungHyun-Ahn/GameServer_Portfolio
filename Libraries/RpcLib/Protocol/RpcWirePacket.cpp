#include "RpcLibPch.h"

#include "Protocol/RpcWirePacket.h"

namespace RpcLib::Protocol
{
	FRpcWirePacket::FRpcWirePacket(
		const ERpcWireOpcode opcode) noexcept
		: m_opcode(opcode)
	{
	}

	FRpcWirePacket::FRpcWirePacket(
		const ERpcWireOpcode opcode,
		std::vector<char> payload) noexcept
		: m_opcode(opcode)
		, m_payload(std::move(payload))
	{
	}

	std::uint16_t FRpcWirePacket::GetOpcode() const noexcept
	{
		return static_cast<std::uint16_t>(m_opcode);
	}

	std::size_t FRpcWirePacket::GetEstimatedBodySize() const noexcept
	{
		return m_payload.size();
	}

	void FRpcWirePacket::Serialize(
		NetworkLib::Packet::Serialization::FPacketWriter& writer) const
	{
		writer.WriteBytes(m_payload.data(), m_payload.size());
	}

	bool FRpcWirePacket::Deserialize(
		NetworkLib::Packet::Serialization::FPacketReader& reader)
	{
		const std::size_t payloadSize = reader.GetRemainingSize();
		if (payloadSize > kMaxRpcPayloadBytes + 128)
		{
			return false;
		}

		m_payload.resize(payloadSize);
		return reader.ReadBytes(m_payload.data(), payloadSize);
	}

	const std::vector<char>& FRpcWirePacket::GetPayload() const noexcept
	{
		return m_payload;
	}
}
