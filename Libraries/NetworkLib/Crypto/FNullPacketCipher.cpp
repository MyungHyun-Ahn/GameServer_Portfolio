#include "NetLibPch.h"

#include "Crypto/FNullPacketCipher.h"

namespace NetworkLib::Crypto
{
	FNullPacketCipher::FNullPacketCipher(
		const SPacketCipherConfig& config)
		: m_config(config)
	{
	}

	void FNullPacketCipher::Encode(
		char* buffer,
		int length,
		std::uint8_t) const noexcept
	{
		UNREFERENCED_PARAMETER(buffer);
		UNREFERENCED_PARAMETER(length);
	}

	void FNullPacketCipher::Decode(
		char* buffer,
		int length,
		std::uint8_t) const noexcept
	{
		UNREFERENCED_PARAMETER(buffer);
		UNREFERENCED_PARAMETER(length);
	}

	std::uint8_t FNullPacketCipher::CalculateChecksum(
		const char* buffer,
		int length) const noexcept
	{
		if (buffer == nullptr || length <= 0)
		{
			return 0;
		}

		std::uint32_t sum = 0;
		for (int index = 0; index < length; ++index)
		{
			sum += static_cast<std::uint8_t>(buffer[index]);
		}

		return static_cast<std::uint8_t>(sum % 256);
	}

	const SPacketCipherConfig& FNullPacketCipher::GetConfig() const noexcept
	{
		return m_config;
	}
}
