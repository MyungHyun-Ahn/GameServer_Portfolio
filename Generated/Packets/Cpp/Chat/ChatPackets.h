#pragma once

namespace Generated::Chat
{
	class FRoomListRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3000;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

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
			return 0;
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return true;
		}
	};

	class FRoomListRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3001;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::vector<std::uint32_t> roomIds{};
		std::vector<std::string> roomNames{};
		std::vector<std::uint32_t> participantCounts{};
		std::vector<std::uint32_t> capacities{};
		std::vector<std::uint8_t> joinableFlags{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomIds) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(roomNames) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(participantCounts) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(capacities) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(joinableFlags);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomIds);
			writer.Write(roomNames);
			writer.Write(participantCounts);
			writer.Write(capacities);
			writer.Write(joinableFlags);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomIds) && reader.Read(roomNames) && reader.Read(participantCounts) && reader.Read(capacities) &&
				   reader.Read(joinableFlags);
		}
	};

	class FRoomEnterRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3002;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint32_t roomId{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomId);
		}
	};

	class FRoomEnterRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3003;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint32_t roomId{};
		bool success{};
		std::uint16_t resultCode{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(roomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(success) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(resultCode);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(roomId);
			writer.Write(success);
			writer.Write(resultCode);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(roomId) && reader.Read(success) && reader.Read(resultCode);
		}
	};

	class FRoomChangeRq final : public NetworkLib::Packet::Serialization::IContentPacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3004;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Request;

		std::uint32_t targetRoomId{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(targetRoomId);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(targetRoomId);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(targetRoomId);
		}
	};

	class FRoomChangeRp final : public NetworkLib::Packet::Serialization::IResponsePacket
	{
	public:
		static constexpr std::uint16_t kOpcode = 3005;
		static constexpr NetworkLib::Packet::Serialization::EContentPacketKind kPacketKind =
			NetworkLib::Packet::Serialization::EContentPacketKind::Response;

		std::uint32_t previousRoomId{};
		std::uint32_t currentRoomId{};
		bool success{};
		std::uint16_t resultCode{};

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
			return NetworkLib::Packet::Serialization::GetSerializedSize(previousRoomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(currentRoomId) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(success) +
				   NetworkLib::Packet::Serialization::GetSerializedSize(resultCode);
		}

		void Serialize(
			NetworkLib::Packet::Serialization::FPacketWriter& writer) const override
		{
			writer.Write(previousRoomId);
			writer.Write(currentRoomId);
			writer.Write(success);
			writer.Write(resultCode);
		}

		bool Deserialize(
			NetworkLib::Packet::Serialization::FPacketReader& reader) override
		{
			return reader.Read(previousRoomId) && reader.Read(currentRoomId) && reader.Read(success) && reader.Read(resultCode);
		}
	};

}
