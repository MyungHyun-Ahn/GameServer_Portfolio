#pragma once

namespace NetworkLib::Packet::View
{
	struct FPacketView
	{
		std::uint16_t opcode = 0;
		std::uint8_t randomKey = 0;
		std::uint8_t checkSum = 0;
		const char* payload = nullptr;
		std::int32_t payloadLength = 0;
	};
}
