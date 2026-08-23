#include "NetLibPch.h"

#include "Packet/Framing/FDefaultPacketFramer.h"

namespace NetworkLib::Packet::Framing
{
	bool FDefaultPacketFramer::BuildPacket(
		const SOutgoingPacket& packet,
		std::vector<char>& outPacket) const
	{
		SFramedPacketBufferParts packetParts{};
		if (!BuildPacketParts(packet, packetParts))
		{
			return false;
		}

		outPacket.resize(sizeof(SPacketHeader) + packet.payloadLength);
		std::memcpy(outPacket.data(), packetParts.headerBytes.data(), packetParts.headerLength);

		if (packet.payloadLength > 0)
		{
			std::memcpy(outPacket.data() + sizeof(SPacketHeader), packet.payload, packet.payloadLength);
		}

		return true;
	}

	bool FDefaultPacketFramer::BuildPacketParts(
		const SOutgoingPacket& packet,
		SFramedPacketBufferParts& outPacketParts) const
	{
		if ((packet.payload == nullptr && packet.payloadLength > 0) || packet.payloadLength < 0 ||
			packet.payloadLength > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max()))
		{
			return false;
		}

		SPacketHeader packetHeader{};
		packetHeader.payloadLength = static_cast<std::uint16_t>(packet.payloadLength);
		packetHeader.randomKey = packet.randomKey;
		packetHeader.checkSum = packet.checkSum;

		outPacketParts.headerLength = static_cast<std::uint32_t>(sizeof(SPacketHeader));
		std::memcpy(outPacketParts.headerBytes.data(), &packetHeader, sizeof(SPacketHeader));
		return true;
	}

	bool FDefaultPacketFramer::TryExtractPacket(
		std::vector<char>& ioBuffer,
		SFramedPacket& outPacket) const
	{
		if (ioBuffer.size() < sizeof(SPacketHeader))
		{
			return false;
		}

		SPacketHeader packetHeader{};
		std::memcpy(&packetHeader, ioBuffer.data(), sizeof(SPacketHeader));

		const std::size_t packetSize = sizeof(SPacketHeader) + packetHeader.payloadLength;
		if (ioBuffer.size() < packetSize)
		{
			return false;
		}

		outPacket.randomKey = packetHeader.randomKey;
		outPacket.checkSum = packetHeader.checkSum;
		outPacket.payload.resize(packetHeader.payloadLength);

		if (packetHeader.payloadLength > 0)
		{
			std::memcpy(outPacket.payload.data(), ioBuffer.data() + sizeof(SPacketHeader), packetHeader.payloadLength);
		}

		ioBuffer.erase(ioBuffer.begin(), ioBuffer.begin() + static_cast<std::ptrdiff_t>(packetSize));
		return true;
	}

	bool FDefaultPacketFramer::TryExtractPacket(
		NetworkLib::Packet::Buffer::FRecvBuffer& ioBuffer,
		SFramedPacket& outPacket) const
	{
		if (ioBuffer.GetUsedSize() < sizeof(SPacketHeader))
		{
			return false;
		}

		SPacketHeader packetHeader{};
		if (!ioBuffer.Peek(&packetHeader, sizeof(SPacketHeader)))
		{
			return false;
		}

		const std::size_t packetSize = sizeof(SPacketHeader) + packetHeader.payloadLength;
		if (ioBuffer.GetUsedSize() < packetSize)
		{
			return false;
		}

		outPacket.randomKey = packetHeader.randomKey;
		outPacket.checkSum = packetHeader.checkSum;
		outPacket.payload.resize(packetHeader.payloadLength);

		if (packetHeader.payloadLength > 0)
		{
			if (!ioBuffer.CopyOut(sizeof(SPacketHeader), outPacket.payload.data(), packetHeader.payloadLength))
			{
				return false;
			}
		}

		return ioBuffer.Discard(packetSize);
	}

	bool FDefaultPacketFramer::TryExtractPacketView(
		NetworkLib::Packet::Buffer::FRecvBuffer& ioBuffer,
		NetworkLib::Packet::View::FPacketView& outPacketView) const
	{
		if (ioBuffer.GetUsedSize() < sizeof(SPacketHeader))
		{
			return false;
		}

		SPacketHeader packetHeader{};
		if (!ioBuffer.Peek(&packetHeader, sizeof(SPacketHeader)))
		{
			return false;
		}

		const std::size_t packetSize = sizeof(SPacketHeader) + packetHeader.payloadLength;
		if (ioBuffer.GetUsedSize() < packetSize)
		{
			return false;
		}

		if (!ioBuffer.EnsureContiguous(packetSize))
		{
			return false;
		}

		const char* packetStart = ioBuffer.GetReadPointer();
		if (packetStart == nullptr)
		{
			return false;
		}

		outPacketView.opcode = 0;
		outPacketView.randomKey = packetHeader.randomKey;
		outPacketView.checkSum = packetHeader.checkSum;
		outPacketView.payload = packetStart + sizeof(SPacketHeader);
		outPacketView.payloadLength = static_cast<std::int32_t>(packetHeader.payloadLength);

		return true;
	}

	std::uint32_t FDefaultPacketFramer::GetHeaderSize() const noexcept
	{
		return static_cast<std::uint32_t>(sizeof(SPacketHeader));
	}
}
