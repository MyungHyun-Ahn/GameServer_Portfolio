#pragma once

namespace NetworkLib::Crypto
{
	struct SPacketCipherConfig
	{
		bool enabled = true;
	};

	struct SDefaultPacketCipherConfig final : public SPacketCipherConfig
	{
		std::uint8_t packetKey = 0;
	};
}
