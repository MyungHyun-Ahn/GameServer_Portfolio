#pragma once

namespace RpcLib::Protocol
{
	enum class ERpcWireOpcode : std::uint16_t
	{
		HelloRequest = 0xFF00,
		HelloResponse = 0xFF01,
		Request = 0xFF02,
		Response = 0xFF03,
		Notification = 0xFF04
	};

	inline constexpr bool IsRpcWireOpcode(
		const std::uint16_t opcode) noexcept
	{
		return opcode >= static_cast<std::uint16_t>(ERpcWireOpcode::HelloRequest) &&
			   opcode <= static_cast<std::uint16_t>(ERpcWireOpcode::Notification);
	}

	class FRpcWirePacket final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		explicit FRpcWirePacket(ERpcWireOpcode opcode) noexcept;
		FRpcWirePacket(ERpcWireOpcode opcode, std::vector<char> payload) noexcept;

		std::uint16_t GetOpcode() const noexcept override;
		std::size_t GetEstimatedBodySize() const noexcept override;
		void Serialize(NetworkLib::Packet::Serialization::FPacketWriter& writer) const override;
		bool Deserialize(NetworkLib::Packet::Serialization::FPacketReader& reader) override;

		const std::vector<char>& GetPayload() const noexcept;

	private:
		ERpcWireOpcode m_opcode;
		std::vector<char> m_payload;
	};
}
