#pragma once

namespace NetworkLib::Crypto
{
	class FDefaultPacketCipher final : public IPacketCipher
	{
	public:
		explicit FDefaultPacketCipher(const SDefaultPacketCipherConfig& config);

	public:
		void Encode(char* buffer, int length, std::uint8_t randomKey) const noexcept override;
		void Decode(char* buffer, int length, std::uint8_t randomKey) const noexcept override;
		std::uint8_t CalculateChecksum(const char* buffer, int length) const noexcept override;
		const SPacketCipherConfig& GetConfig() const noexcept override;

	private:
		SDefaultPacketCipherConfig m_config;
	};
}
