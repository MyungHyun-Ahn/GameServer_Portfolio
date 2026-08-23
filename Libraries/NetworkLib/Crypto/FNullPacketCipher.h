#pragma once

namespace NetworkLib::Crypto
{
	class FNullPacketCipher final : public IPacketCipher
	{
	public:
		explicit FNullPacketCipher(const SPacketCipherConfig& config = {});

	public:
		void Encode(char* buffer, int length, std::uint8_t randomKey) const noexcept override;
		void Decode(char* buffer, int length, std::uint8_t randomKey) const noexcept override;
		std::uint8_t CalculateChecksum(const char* buffer, int length) const noexcept override;
		const SPacketCipherConfig& GetConfig() const noexcept override;

	private:
		SPacketCipherConfig m_config;
	};
}
