#pragma once

namespace Generated::Login
{
	class FLoginRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 2000;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint32_t userId{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(userId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(userId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(userId);
		}
	};

	class FLoginRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 2001;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint32_t userId{};
		bool success{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(userId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(success);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(userId);
			writer.Write(success);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(userId) && reader.Read(success);
		}
	};

	class FLoginAuthRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 2002;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::string ticket{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(ticket);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(ticket);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(ticket);
		}
	};

	class FLoginAuthRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 2003;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint32_t userId{};
		bool success{};

	public:
		std::uint16_t GetOpcode() const noexcept override
		{
			return kOpcode;
		}

		bool ContainsBorrowedViews() const noexcept override
		{
			return false;
		}

		std::size_t GetEstimatedBodySize() const noexcept override
		{
			return NetworkLib::Packet::Serialization::GetSerializedSize(userId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(success);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(userId);
			writer.Write(success);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(userId) && reader.Read(success);
		}
	};

}
