#pragma once

namespace NetworkLib::Packet::Framing
{
	class FDefaultPacketFramer final : public IPacketFramer
	{
	public:
		bool BuildPacket(const SOutgoingPacket& packet, std::vector<char>& outPacket) const override;
		bool BuildPacketParts(const SOutgoingPacket& packet, SFramedPacketBufferParts& outPacketParts) const override;
		bool TryExtractPacket(std::vector<char>& ioBuffer, SFramedPacket& outPacket) const override;
		bool TryExtractPacket(NetworkLib::Packet::Buffer::FRecvBuffer& ioBuffer, SFramedPacket& outPacket) const override;
		bool TryExtractPacketView(NetworkLib::Packet::Buffer::FRecvBuffer& ioBuffer,
			NetworkLib::Packet::View::FPacketView& outPacketView) const override;
		std::uint32_t GetHeaderSize() const noexcept override;
	};
}
