#pragma once

namespace NetworkLib::Crypto
{
	class IPacketCipher
	{
	public:
		virtual ~IPacketCipher() = default;

	public:
		virtual void Encode(char* buffer, int length, std::uint8_t randomKey) const noexcept = 0;
		virtual void Decode(char* buffer, int length, std::uint8_t randomKey) const noexcept = 0;
		virtual std::uint8_t CalculateChecksum(const char* buffer, int length) const noexcept = 0;
		virtual const SPacketCipherConfig& GetConfig() const noexcept = 0;
	};
}
