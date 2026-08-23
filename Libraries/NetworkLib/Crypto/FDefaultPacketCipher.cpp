#include "NetLibPch.h"

#include "Crypto/FDefaultPacketCipher.h"

namespace
{
	using TByte = std::uint8_t;

	TByte ToByte(
		char value) noexcept
	{
		return static_cast<TByte>(value);
	}

	char ToChar(
		TByte value) noexcept
	{
		return static_cast<char>(value);
	}
}

namespace NetworkLib::Crypto
{
	FDefaultPacketCipher::FDefaultPacketCipher(
		const SDefaultPacketCipherConfig& config)
		: m_config(config)
	{
	}

	void FDefaultPacketCipher::Encode(
		char* buffer,
		int length,
		std::uint8_t randomKey) const noexcept
	{
		if (!m_config.enabled || buffer == nullptr || length <= 0)
		{
			return;
		}

		TByte plainState = 0;
		TByte encodedState = 0;
		const TByte randomKeyPlusOne = static_cast<TByte>(randomKey + 1);
		const TByte packetKeyPlusOne = static_cast<TByte>(m_config.packetKey + 1);

		for (int index = 0; index < length; ++index)
		{
			const TByte plainValue = ToByte(buffer[index]);
			plainState = static_cast<TByte>(plainValue ^ static_cast<TByte>(plainState + randomKeyPlusOne + index));
			encodedState = static_cast<TByte>(plainState ^ static_cast<TByte>(encodedState + packetKeyPlusOne + index));
			buffer[index] = ToChar(encodedState);
		}
	}

	void FDefaultPacketCipher::Decode(
		char* buffer,
		int length,
		std::uint8_t randomKey) const noexcept
	{
		if (!m_config.enabled || buffer == nullptr || length <= 0)
		{
			return;
		}

		TByte previousPlainState = 0;
		TByte previousEncodedState = 0;
		const TByte randomKeyPlusOne = static_cast<TByte>(randomKey + 1);
		const TByte packetKeyPlusOne = static_cast<TByte>(m_config.packetKey + 1);

		for (int index = 0; index < length; ++index)
		{
			const TByte encodedValue = ToByte(buffer[index]);
			const TByte plainState = static_cast<TByte>(encodedValue ^ static_cast<TByte>(previousEncodedState + packetKeyPlusOne + index));
			const TByte decodedValue = static_cast<TByte>(plainState ^ static_cast<TByte>(previousPlainState + randomKeyPlusOne + index));
			buffer[index] = ToChar(decodedValue);
			previousEncodedState = encodedValue;
			previousPlainState = plainState;
		}
	}

	std::uint8_t FDefaultPacketCipher::CalculateChecksum(
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
			sum += ToByte(buffer[index]);
		}

		return static_cast<std::uint8_t>(sum % 256);
	}

	const SPacketCipherConfig& FDefaultPacketCipher::GetConfig() const noexcept
	{
		return m_config;
	}
}
